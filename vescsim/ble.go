//go:build darwin

package main

/*
#cgo CFLAGS: -x objective-c
#cgo LDFLAGS: -framework CoreBluetooth -framework Foundation

#include <stdlib.h>
#include "ble_darwin.h"
*/
import "C"

import (
	"bytes"
	"fmt"
	"log"
	"sync"
	"time"
	"unsafe"

)

// VESC Express NUS (Nordic UART Service) UUIDs
const (
	vescServiceUUIDStr = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
	vescRXCharUUIDStr  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
	vescTXCharUUIDStr  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
)

func bleChunkSize() int {
	mtu := int(C.bleGetMTU())
	if mtu < 20 {
		return 20
	}
	return mtu
}

var (
	bleSim   *Simulator
	bleRxBuf bytes.Buffer
	bleRxMu  sync.Mutex
)

//export goBLEDidReceiveWrite
func goBLEDidReceiveWrite(data unsafe.Pointer, length C.int) {
	if bleSim == nil {
		return
	}
	buf := C.GoBytes(data, length)
	bleRxMu.Lock()
	bleRxBuf.Write(buf)
	bleRxMu.Unlock()
	processBLEBuffer()
}

//export goBLEDidSubscribe
func goBLEDidSubscribe() {
	log.Println("simulator: BLE client subscribed to TX notifications")
}

//export goBLEDidUnsubscribe
func goBLEDidUnsubscribe() {
	log.Println("simulator: BLE client unsubscribed from TX notifications")
}

//export goBLEStateUpdate
func goBLEStateUpdate(state C.int) {
	switch int(state) {
	case 5:
		log.Println("simulator: BLE powered on")
	case 4:
		log.Println("simulator: BLE powered off")
	case 1:
		log.Println("simulator: BLE resetting")
	case 3:
		log.Println("simulator: BLE unauthorized — check System Settings > Privacy > Bluetooth")
	case 2:
		log.Println("simulator: BLE unsupported on this hardware")
	}
}

func processBLEBuffer() {
	bleRxMu.Lock()
	defer bleRxMu.Unlock()

	for bleRxBuf.Len() > 0 {
		data := bleRxBuf.Bytes()
		pktLen, ok := peekPacketLen(data)
		if !ok {
			// Invalid start byte — discard one byte and retry
			bleRxBuf.ReadByte()
			continue
		}
		if bleRxBuf.Len() < pktLen {
			return
		}

		pktData := make([]byte, pktLen)
		bleRxBuf.Read(pktData)

		reader := bytes.NewReader(pktData)
		payload, err := DecodePacket(reader)
		if err != nil {
			log.Printf("simulator: BLE decode error: %v", err)
			continue
		}

		resp := bleSim.HandleCommand(payload)
		if resp == nil {
			continue
		}
		respPkt, err := EncodePacket(resp)
		if err != nil {
			log.Printf("simulator: BLE encode error: %v", err)
			continue
		}
		logTx(resp)

		mtu := bleChunkSize()
		for len(respPkt) > 0 {
			chunk := respPkt
			if len(chunk) > mtu {
				chunk = chunk[:mtu]
			}
			respPkt = respPkt[len(chunk):]

			cData := C.CBytes(chunk)
			rc := C.bleSendNotification(cData, C.int(len(chunk)))
			C.free(cData)
			if rc != 0 {
				log.Printf("simulator: BLE tx error (rc=%d)", rc)
				return
			}
		}
	}
}

// StartBLE starts the BLE GATT server emulating a VESC Express using CoreBluetooth.
func (s *Simulator) StartBLE(name string) error {
	s.mu.Lock()
	s.bleName = name
	if s.bleName == "" {
		s.bleName = "VESC SIM"
	}
	localName := s.bleName // snapshot for use outside lock
	s.mu.Unlock()

	bleSim = s

	cName := C.CString(localName)
	defer C.free(unsafe.Pointer(cName))
	cService := C.CString(vescServiceUUIDStr)
	defer C.free(unsafe.Pointer(cService))
	cRX := C.CString(vescRXCharUUIDStr)
	defer C.free(unsafe.Pointer(cRX))
	cTX := C.CString(vescTXCharUUIDStr)
	defer C.free(unsafe.Pointer(cTX))

	C.bleInit(cName, cService, cRX, cTX)

	for i := 0; i < 50; i++ {
		time.Sleep(100 * time.Millisecond)
		if C.bleIsReady() != 0 {
			log.Printf("simulator: BLE advertising as %q", localName)
			return nil
		}
	}
	return fmt.Errorf("BLE did not become ready (check Bluetooth is enabled and app is authorized)")
}

// peekPacketLen looks at the start of data and returns the total framed packet length.
func peekPacketLen(data []byte) (int, bool) {
	if len(data) == 0 {
		return 0, false
	}

	switch data[0] {
	case 0x02:
		if len(data) < 2 {
			return 0, false
		}
		return 1 + 1 + int(data[1]) + 2 + 1, true
	case 0x03:
		if len(data) < 3 {
			return 0, false
		}
		payloadLen := int(data[1])<<8 | int(data[2])
		if payloadLen == 0 || payloadLen > maxPayloadSize {
			return 0, false
		}
		return 1 + 2 + payloadLen + 2 + 1, true
	default:
		// Invalid start byte — skip it so the caller can resync
		return 0, false
	}
}
