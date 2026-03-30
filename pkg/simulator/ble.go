package simulator

import (
	"bytes"
	"log"

	"github.com/szatmary/nosedive/pkg/vesc"

	"tinygo.org/x/bluetooth"
)

// VESC Express NUS (Nordic UART Service) UUIDs
var (
	vescServiceUUID = bluetooth.NewUUID([16]byte{
		0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
		0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
	})
	vescRXCharUUID = bluetooth.NewUUID([16]byte{
		0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
		0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e,
	})
	vescTXCharUUID = bluetooth.NewUUID([16]byte{
		0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
		0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e,
	})
)

const bleChunkSize = 20 // Default BLE MTU payload size

// StartBLE starts the BLE GATT server emulating a VESC Express.
func (s *Simulator) StartBLE(name string) error {
	s.bleName = name
	if s.bleName == "" {
		s.bleName = "VESC SIM"
	}

	adapter := bluetooth.DefaultAdapter
	if err := adapter.Enable(); err != nil {
		return err
	}

	// Configure advertisement
	adv := adapter.DefaultAdvertisement()
	if err := adv.Configure(bluetooth.AdvertisementOptions{
		LocalName:    s.bleName,
		ServiceUUIDs: []bluetooth.UUID{vescServiceUUID},
	}); err != nil {
		return err
	}

	var txChar bluetooth.Characteristic

	// Accumulation buffer for incoming BLE writes (packets may be chunked)
	var rxBuf bytes.Buffer

	// Add GATT service
	if err := adapter.AddService(&bluetooth.Service{
		UUID: vescServiceUUID,
		Characteristics: []bluetooth.CharacteristicConfig{
			{
				// RX: client writes VESC packets here
				UUID:  vescRXCharUUID,
				Flags: bluetooth.CharacteristicWritePermission | bluetooth.CharacteristicWriteWithoutResponsePermission,
				WriteEvent: func(client bluetooth.Connection, offset int, value []byte) {
					rxBuf.Write(value)
					s.processBLEData(&rxBuf, &txChar)
				},
			},
			{
				// TX: simulator sends responses via notifications
				Handle: &txChar,
				UUID:   vescTXCharUUID,
				Flags:  bluetooth.CharacteristicNotifyPermission | bluetooth.CharacteristicReadPermission,
			},
		},
	}); err != nil {
		return err
	}

	// Start advertising
	if err := adv.Start(); err != nil {
		return err
	}

	log.Printf("simulator: BLE advertising as %q", s.bleName)
	return nil
}

// processBLEData attempts to decode complete VESC packets from the buffer,
// process them, and send responses back via the TX characteristic.
func (s *Simulator) processBLEData(buf *bytes.Buffer, txChar *bluetooth.Characteristic) {
	for buf.Len() > 0 {
		// Peek at the buffer to see if we have a complete packet
		data := buf.Bytes()
		pktLen, ok := peekPacketLen(data)
		if !ok || buf.Len() < pktLen {
			return // need more data
		}

		// Extract the full packet
		pktData := make([]byte, pktLen)
		buf.Read(pktData)

		// Decode the VESC packet
		reader := bytes.NewReader(pktData)
		payload, err := vesc.DecodePacket(reader)
		if err != nil {
			log.Printf("simulator: BLE decode error: %v", err)
			// Discard this byte and try to resync
			continue
		}

		// Process the command
		resp := s.HandleCommand(payload)
		if resp == nil {
			continue
		}

		// Encode response packet
		respPkt, err := vesc.EncodePacket(resp)
		if err != nil {
			log.Printf("simulator: BLE encode error: %v", err)
			continue
		}

		// Send response chunked to BLE MTU size
		for len(respPkt) > 0 {
			chunk := respPkt
			if len(chunk) > bleChunkSize {
				chunk = chunk[:bleChunkSize]
			}
			respPkt = respPkt[len(chunk):]

			if _, err := txChar.Write(chunk); err != nil {
				log.Printf("simulator: BLE tx error: %v", err)
				return
			}
		}
	}
}

// peekPacketLen looks at the start of data and returns the total framed packet
// length if a valid start byte is found. Returns (length, true) if determinable.
func peekPacketLen(data []byte) (int, bool) {
	if len(data) == 0 {
		return 0, false
	}

	switch data[0] {
	case 0x02: // short packet: [0x02][len:1][payload][crc:2][0x03]
		if len(data) < 2 {
			return 0, false
		}
		payloadLen := int(data[1])
		totalLen := 1 + 1 + payloadLen + 2 + 1
		return totalLen, true

	case 0x03: // long packet: [0x03][len:2][payload][crc:2][0x03]
		if len(data) < 3 {
			return 0, false
		}
		payloadLen := int(data[1])<<8 | int(data[2])
		totalLen := 1 + 2 + payloadLen + 2 + 1
		return totalLen, true

	default:
		// Not a valid start byte - skip it
		return 1, true
	}
}
