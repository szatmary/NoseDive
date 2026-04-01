package main

import (
	"encoding/binary"
	"fmt"
	"io"
	"math"
)

// ---------------------------------------------------------------------------
// CRC16 lookup table from VESC firmware (util/crc.c)
// This is a standard CRC-CCITT (XModem) table.
// ---------------------------------------------------------------------------

var crc16Tab = [256]uint16{
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
	0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
	0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
	0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
	0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
	0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
	0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
	0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
	0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
	0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
	0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
	0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
	0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
	0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
}

// CRC16 computes the CRC-16/CCITT checksum used by VESC.
func CRC16(data []byte) uint16 {
	var cksum uint16
	for _, b := range data {
		cksum = crc16Tab[((cksum>>8)^uint16(b))&0xFF] ^ (cksum << 8)
	}
	return cksum
}

// ---------------------------------------------------------------------------
// VESC command identifiers (from datatypes.h)
// ---------------------------------------------------------------------------

// CommPacketID represents VESC command identifiers from datatypes.h (0-159)
type CommPacketID uint8

const (
	CommFWVersion                    CommPacketID = 0
	CommJumpToBootloader             CommPacketID = 1
	CommEraseNewApp                  CommPacketID = 2
	CommWriteNewAppData              CommPacketID = 3
	CommGetValues                    CommPacketID = 4
	CommSetDuty                      CommPacketID = 5
	CommSetCurrent                   CommPacketID = 6
	CommSetCurrentBrake              CommPacketID = 7
	CommSetRPM                       CommPacketID = 8
	CommSetPos                       CommPacketID = 9
	CommSetHandbrake                 CommPacketID = 10
	CommSetDetect                    CommPacketID = 11
	CommSetServoPos                  CommPacketID = 12
	CommSetMCConf                    CommPacketID = 13
	CommGetMCConf                    CommPacketID = 14
	CommGetMCConfDefault             CommPacketID = 15
	CommSetAppConf                   CommPacketID = 16
	CommGetAppConf                   CommPacketID = 17
	CommGetAppConfDefault            CommPacketID = 18
	CommSamplePrint                  CommPacketID = 19
	CommTerminalCmd                  CommPacketID = 20
	CommPrintText                    CommPacketID = 21
	CommRotor                        CommPacketID = 22
	CommExperiment                   CommPacketID = 23
	CommDetectMotorParam             CommPacketID = 24
	CommDetectMotorRL                CommPacketID = 25
	CommDetectMotorFlux              CommPacketID = 26
	CommDetectEncoder                CommPacketID = 27
	CommDetectHallFOC                CommPacketID = 28
	CommReboot                       CommPacketID = 29
	CommAlive                        CommPacketID = 30
	CommGetDecodedPPM                CommPacketID = 31
	CommGetDecodedADC                CommPacketID = 32
	CommGetDecodedChuk               CommPacketID = 33
	CommForwardCAN                   CommPacketID = 34
	CommSetChuckData                 CommPacketID = 35
	CommCustomAppData                CommPacketID = 36
	CommNRFStartPairing              CommPacketID = 37
	CommGetValuesSetup               CommPacketID = 47
	CommSetMCConfTemp                CommPacketID = 48
	CommSetMCConfTempSetup           CommPacketID = 49
	CommGetValuesSelective           CommPacketID = 50
	CommGetValuesSetupSelective      CommPacketID = 51
	CommDetectMotorFluxOpenloop      CommPacketID = 57
	CommDetectApplyAllFOC            CommPacketID = 58
	CommPingCAN                      CommPacketID = 62
	CommAppDisableOutput             CommPacketID = 63
	CommTerminalCmdSync              CommPacketID = 64
	CommGetIMUData                   CommPacketID = 65
	CommGetDecodedBalance            CommPacketID = 79
	CommSetCurrentRel                CommPacketID = 84
	CommCANFwdFrame                  CommPacketID = 85
	CommSetBatteryCut                CommPacketID = 86
	CommSetBLEName                   CommPacketID = 87
	CommSetBLEPin                    CommPacketID = 88
	CommSetCANMode                   CommPacketID = 89
	CommGetIMUCalibration            CommPacketID = 90
	CommGetMCConfTemp                CommPacketID = 91
	CommGetCustomConfigXML           CommPacketID = 92
	CommGetCustomConfig              CommPacketID = 93
	CommGetCustomConfigDefault       CommPacketID = 94
	CommSetCustomConfig              CommPacketID = 95
	CommBMSGetValues                 CommPacketID = 96
	CommSetOdometer                  CommPacketID = 110
	CommGetBatteryCut                CommPacketID = 115
	CommGetQMLUIHW                   CommPacketID = 117
	CommGetQMLUIApp                  CommPacketID = 118
	CommCustomHWData                 CommPacketID = 119
	CommGetStats                     CommPacketID = 128
	CommResetStats                   CommPacketID = 129
	CommGetGNSS                      CommPacketID = 150
	CommShutdown                     CommPacketID = 156
	CommFWInfo                       CommPacketID = 157
	CommMotorEStop                   CommPacketID = 159
)

// HWType from VESC datatypes.h
type HWType uint8

const (
	HWTypeVESC        HWType = 0
	HWTypeVESCExpress HWType = 3
)

// FaultCode from VESC datatypes.h
type FaultCode uint8

const (
	FaultNone                   FaultCode = 0
	FaultOverVoltage            FaultCode = 1
	FaultUnderVoltage           FaultCode = 2
	FaultDRV                    FaultCode = 3
	FaultAbsOverCurrent         FaultCode = 4
	FaultOverTempFET            FaultCode = 5
	FaultOverTempMotor          FaultCode = 6
	FaultGateDriverOverVoltage  FaultCode = 7
	FaultGateDriverUnderVoltage FaultCode = 8
	FaultMCUUnderVoltage        FaultCode = 9
	FaultBootingFromWatchdog    FaultCode = 10
	FaultEncoderSPI             FaultCode = 11
)

func (f FaultCode) String() string {
	switch f {
	case FaultNone:
		return "NONE"
	case FaultOverVoltage:
		return "OVER_VOLTAGE"
	case FaultUnderVoltage:
		return "UNDER_VOLTAGE"
	case FaultDRV:
		return "DRV"
	case FaultAbsOverCurrent:
		return "ABS_OVER_CURRENT"
	case FaultOverTempFET:
		return "OVER_TEMP_FET"
	case FaultOverTempMotor:
		return "OVER_TEMP_MOTOR"
	default:
		return "UNKNOWN"
	}
}

// Values represents the data returned by COMM_GET_VALUES.
type Values struct {
	TempMOSFET       float64
	TempMotor        float64
	AvgMotorCurrent  float64
	AvgInputCurrent  float64
	AvgID            float64
	AvgIQ            float64
	DutyCycle        float64
	RPM              float64
	Voltage          float64
	AmpHours         float64
	AmpHoursCharged  float64
	WattHours        float64
	WattHoursCharged float64
	Tachometer       int32
	TachometerAbs    int32
	Fault            FaultCode
	PIDPos           float64
	ControllerID     uint8
	TempMOS1         float64
	TempMOS2         float64
	TempMOS3         float64
	AvgVd            float64
	AvgVq            float64
	Status           uint8
}

// FWVersion represents firmware version info.
type FWVersion struct {
	Major  uint8
	Minor  uint8
	HWName string
	UUID   []byte
}

// ---------------------------------------------------------------------------
// Packet encoding / decoding
// ---------------------------------------------------------------------------

const (
	startByteShort byte = 0x02 // Payload length < 256 bytes
	startByteLong  byte = 0x03 // Payload length >= 256 bytes
	endByte        byte = 0x03

	maxPayloadSize = 8192 // Large enough for config chunks and QML data
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

// ---------------------------------------------------------------------------
// Buffer helpers matching VESC's buffer.h conventions (big-endian).
// ---------------------------------------------------------------------------

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

// GetFloat32Auto decodes VESC's float32_auto format: IEEE754 float32, big-endian (4 bytes).
// Despite the name "auto", this is equivalent to a standard IEEE754 float32.
func GetFloat32Auto(buf []byte, idx *int) float64 {
	bits := binary.BigEndian.Uint32(buf[*idx:])
	*idx += 4
	return float64(math.Float32frombits(bits))
}

// AppendFloat32Auto encodes a float using VESC's float32_auto format (4 bytes).
// This is a standard IEEE754 float32, stored big-endian.
func AppendFloat32Auto(buf []byte, v float64) []byte {
	bits := math.Float32bits(float32(v))
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, bits)
	return append(buf, b...)
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
	v.RPM = GetFloat32(data, 1, &idx)
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
