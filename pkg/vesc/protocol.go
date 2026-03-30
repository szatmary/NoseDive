package vesc

import (
	"encoding/binary"
	"fmt"
	"io"
	"math"
)

const (
	startByteShort byte = 0x02 // Payload length < 256 bytes
	startByteLong  byte = 0x03 // Payload length >= 256 bytes
	endByte        byte = 0x03

	maxPayloadSize = 512
)

// EncodePacket frames a VESC payload into a wire-format packet.
//
// Short packet (payload < 256 bytes):
//
//	[0x02][len:1][payload][crc16:2][0x03]
//
// Long packet (payload >= 256 bytes):
//
//	[0x03][len:2][payload][crc16:2][0x03]
func EncodePacket(payload []byte) ([]byte, error) {
	n := len(payload)
	if n == 0 {
		return nil, fmt.Errorf("empty payload")
	}
	if n > maxPayloadSize {
		return nil, fmt.Errorf("payload too large: %d > %d", n, maxPayloadSize)
	}

	crc := CRC16(payload)

	if n < 256 {
		pkt := make([]byte, 0, 1+1+n+2+1)
		pkt = append(pkt, startByteShort)
		pkt = append(pkt, byte(n))
		pkt = append(pkt, payload...)
		pkt = append(pkt, byte(crc>>8), byte(crc&0xFF))
		pkt = append(pkt, endByte)
		return pkt, nil
	}

	pkt := make([]byte, 0, 1+2+n+2+1)
	pkt = append(pkt, startByteLong)
	pkt = append(pkt, byte(n>>8), byte(n&0xFF))
	pkt = append(pkt, payload...)
	pkt = append(pkt, byte(crc>>8), byte(crc&0xFF))
	pkt = append(pkt, endByte)
	return pkt, nil
}

// DecodePacket reads one VESC packet from the reader and returns the payload.
// It scans for a valid start byte, then reads the rest of the frame.
func DecodePacket(r io.Reader) ([]byte, error) {
	buf := make([]byte, 1)

	// Scan for start byte
	for {
		if _, err := io.ReadFull(r, buf); err != nil {
			return nil, err
		}
		if buf[0] == startByteShort || buf[0] == startByteLong {
			break
		}
	}

	startType := buf[0]
	var payloadLen int

	if startType == startByteShort {
		if _, err := io.ReadFull(r, buf); err != nil {
			return nil, err
		}
		payloadLen = int(buf[0])
	} else {
		lenBuf := make([]byte, 2)
		if _, err := io.ReadFull(r, lenBuf); err != nil {
			return nil, err
		}
		payloadLen = int(binary.BigEndian.Uint16(lenBuf))
	}

	if payloadLen == 0 || payloadLen > maxPayloadSize {
		return nil, fmt.Errorf("invalid payload length: %d", payloadLen)
	}

	// Read payload + 2 byte CRC + 1 byte end
	frame := make([]byte, payloadLen+3)
	if _, err := io.ReadFull(r, frame); err != nil {
		return nil, err
	}

	payload := frame[:payloadLen]
	crcReceived := binary.BigEndian.Uint16(frame[payloadLen : payloadLen+2])
	if frame[payloadLen+2] != endByte {
		return nil, fmt.Errorf("missing end byte, got 0x%02x", frame[payloadLen+2])
	}

	crcCalc := CRC16(payload)
	if crcCalc != crcReceived {
		return nil, fmt.Errorf("CRC mismatch: calculated 0x%04x, received 0x%04x", crcCalc, crcReceived)
	}

	return payload, nil
}

// Buffer helpers matching VESC's buffer.h conventions (big-endian).

func AppendInt16(buf []byte, v int16) []byte {
	return append(buf, byte(v>>8), byte(v))
}

func AppendUint16(buf []byte, v uint16) []byte {
	return append(buf, byte(v>>8), byte(v))
}

func AppendInt32(buf []byte, v int32) []byte {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, uint32(v))
	return append(buf, b...)
}

func AppendUint32(buf []byte, v uint32) []byte {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, v)
	return append(buf, b...)
}

func AppendFloat16(buf []byte, v float64, scale float64) []byte {
	return AppendInt16(buf, int16(v*scale))
}

func AppendFloat32(buf []byte, v float64, scale float64) []byte {
	return AppendInt32(buf, int32(v*scale))
}

func GetInt16(buf []byte, idx *int) int16 {
	v := int16(buf[*idx])<<8 | int16(buf[*idx+1])
	*idx += 2
	return v
}

func GetUint16(buf []byte, idx *int) uint16 {
	v := uint16(buf[*idx])<<8 | uint16(buf[*idx+1])
	*idx += 2
	return v
}

func GetInt32(buf []byte, idx *int) int32 {
	v := int32(binary.BigEndian.Uint32(buf[*idx:]))
	*idx += 4
	return v
}

func GetUint32(buf []byte, idx *int) uint32 {
	v := binary.BigEndian.Uint32(buf[*idx:])
	*idx += 4
	return v
}

func GetFloat16(buf []byte, scale float64, idx *int) float64 {
	return float64(GetInt16(buf, idx)) / scale
}

func GetFloat32(buf []byte, scale float64, idx *int) float64 {
	return float64(GetInt32(buf, idx)) / scale
}

func GetFloat32Auto(buf []byte, idx *int) float64 {
	// VESC float32_auto encoding: first 4 bytes contain IEEE754 float
	bits := binary.BigEndian.Uint32(buf[*idx:])
	*idx += 4
	return float64(math.Float32frombits(bits))
}

func GetString(buf []byte, idx *int) string {
	start := *idx
	for *idx < len(buf) && buf[*idx] != 0 {
		*idx++
	}
	s := string(buf[start:*idx])
	if *idx < len(buf) {
		*idx++ // skip null terminator
	}
	return s
}

// ParseValues decodes a COMM_GET_VALUES response payload (after command byte).
func ParseValues(data []byte) (*Values, error) {
	if len(data) < 53 {
		return nil, fmt.Errorf("COMM_GET_VALUES response too short: %d bytes", len(data))
	}
	idx := 0
	v := &Values{}
	v.TempMOSFET = GetFloat16(data, 10, &idx)
	v.TempMotor = GetFloat16(data, 10, &idx)
	v.AvgMotorCurrent = GetFloat32(data, 100, &idx)
	v.AvgInputCurrent = GetFloat32(data, 100, &idx)
	idx += 4 // skip id
	idx += 4 // skip iq
	v.DutyCycle = GetFloat16(data, 1000, &idx)
	v.RPM = float64(GetInt32(data, &idx))
	v.Voltage = GetFloat16(data, 10, &idx)
	v.AmpHours = GetFloat32(data, 10000, &idx)
	v.AmpHoursCharged = GetFloat32(data, 10000, &idx)
	v.WattHours = GetFloat32(data, 10000, &idx)
	v.WattHoursCharged = GetFloat32(data, 10000, &idx)
	v.Tachometer = GetInt32(data, &idx)
	v.TachometerAbs = GetInt32(data, &idx)
	v.Fault = FaultCode(data[idx])
	return v, nil
}

// BuildGetValues creates a COMM_GET_VALUES request payload.
func BuildGetValues() []byte {
	return []byte{byte(CommGetValues)}
}

// BuildFWVersion creates a COMM_FW_VERSION request payload.
func BuildFWVersion() []byte {
	return []byte{byte(CommFWVersion)}
}

// BuildAlive creates a COMM_ALIVE keepalive payload.
func BuildAlive() []byte {
	return []byte{byte(CommAlive)}
}

// BuildCustomAppData creates a COMM_CUSTOM_APP_DATA payload.
func BuildCustomAppData(data []byte) []byte {
	payload := make([]byte, 1, 1+len(data))
	payload[0] = byte(CommCustomAppData)
	payload = append(payload, data...)
	return payload
}
