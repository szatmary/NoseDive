package vesc

import (
	"bytes"
	"fmt"
	"log"
	"sync"
	"time"

	"tinygo.org/x/bluetooth"
)

// VESC Express NUS (Nordic UART Service) UUIDs
var (
	VESCServiceUUID = bluetooth.NewUUID([16]byte{
		0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
		0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
	})
	VESCRXCharUUID = bluetooth.NewUUID([16]byte{
		0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
		0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e,
	})
	VESCTXCharUUID = bluetooth.NewUUID([16]byte{
		0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
		0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e,
	})
)

const bleWriteChunk = 20

// BLEDevice represents a discovered VESC BLE device.
type BLEDevice struct {
	Name    string
	Address string
	RSSI    int16
	result  bluetooth.ScanResult
}

// ScanBLE scans for VESC BLE devices for the given duration.
func ScanBLE(timeout time.Duration) ([]BLEDevice, error) {
	adapter := bluetooth.DefaultAdapter
	if err := adapter.Enable(); err != nil {
		return nil, fmt.Errorf("enable BLE: %w", err)
	}

	var devices []BLEDevice
	var mu sync.Mutex
	seen := make(map[string]bool)

	done := make(chan struct{})
	go func() {
		time.Sleep(timeout)
		adapter.StopScan()
		close(done)
	}()

	err := adapter.Scan(func(adapter *bluetooth.Adapter, result bluetooth.ScanResult) {
		if !result.HasServiceUUID(VESCServiceUUID) {
			return
		}
		addr := result.Address.String()
		mu.Lock()
		defer mu.Unlock()
		if seen[addr] {
			return
		}
		seen[addr] = true
		devices = append(devices, BLEDevice{
			Name:    result.LocalName(),
			Address: addr,
			RSSI:    result.RSSI,
			result:  result,
		})
		log.Printf("Found VESC: %s (%s) RSSI: %d", result.LocalName(), addr, result.RSSI)
	})

	<-done
	if err != nil {
		return devices, fmt.Errorf("scan: %w", err)
	}
	return devices, nil
}

// BLEConnection wraps a BLE connection to a VESC as an io.ReadWriteCloser
// compatible with vesc.Connection.
type BLEConnection struct {
	device bluetooth.Device
	rxChar bluetooth.DeviceCharacteristic // we write to this (VESC RX)
	txChar bluetooth.DeviceCharacteristic // we read from this (VESC TX)

	mu     sync.Mutex
	rxBuf  bytes.Buffer
	notify chan struct{}
	closed bool
}

// DialBLE connects to a VESC BLE device by address.
func DialBLE(address string) (*Connection, error) {
	adapter := bluetooth.DefaultAdapter
	if err := adapter.Enable(); err != nil {
		return nil, fmt.Errorf("enable BLE: %w", err)
	}

	// Parse the address
	var addr bluetooth.Address
	addr.Set(address)

	device, err := adapter.Connect(addr, bluetooth.ConnectionParams{
		ConnectionTimeout: bluetooth.NewDuration(10 * time.Second),
	})
	if err != nil {
		return nil, fmt.Errorf("connect to %s: %w", address, err)
	}

	// Discover the VESC UART service
	services, err := device.DiscoverServices([]bluetooth.UUID{VESCServiceUUID})
	if err != nil {
		device.Disconnect()
		return nil, fmt.Errorf("discover services: %w", err)
	}
	if len(services) == 0 {
		device.Disconnect()
		return nil, fmt.Errorf("VESC UART service not found")
	}

	// Discover characteristics
	chars, err := services[0].DiscoverCharacteristics([]bluetooth.UUID{VESCRXCharUUID, VESCTXCharUUID})
	if err != nil {
		device.Disconnect()
		return nil, fmt.Errorf("discover characteristics: %w", err)
	}
	if len(chars) < 2 {
		device.Disconnect()
		return nil, fmt.Errorf("expected 2 characteristics, found %d", len(chars))
	}

	bleConn := &BLEConnection{
		device: device,
		notify: make(chan struct{}, 1),
	}

	// Identify RX and TX by UUID
	for _, c := range chars {
		if c.UUID() == VESCRXCharUUID {
			bleConn.rxChar = c
		} else if c.UUID() == VESCTXCharUUID {
			bleConn.txChar = c
		}
	}

	// Enable notifications on TX characteristic
	err = bleConn.txChar.EnableNotifications(func(buf []byte) {
		bleConn.mu.Lock()
		bleConn.rxBuf.Write(buf)
		bleConn.mu.Unlock()
		select {
		case bleConn.notify <- struct{}{}:
		default:
		}
	})
	if err != nil {
		device.Disconnect()
		return nil, fmt.Errorf("enable notifications: %w", err)
	}

	return NewConnection(bleConn), nil
}

// DialBLEDevice connects to a previously scanned BLE device.
func DialBLEDevice(dev BLEDevice) (*Connection, error) {
	return DialBLE(dev.Address)
}

// Read implements io.Reader. It blocks until data is available from BLE notifications.
func (c *BLEConnection) Read(p []byte) (int, error) {
	for {
		c.mu.Lock()
		if c.closed {
			c.mu.Unlock()
			return 0, fmt.Errorf("connection closed")
		}
		n := c.rxBuf.Len()
		if n > 0 {
			nr, err := c.rxBuf.Read(p)
			c.mu.Unlock()
			return nr, err
		}
		c.mu.Unlock()

		// Wait for notification
		<-c.notify
	}
}

// Write implements io.Writer. It chunks data to fit BLE MTU and writes to RX.
func (c *BLEConnection) Write(p []byte) (int, error) {
	total := len(p)
	for len(p) > 0 {
		chunk := p
		if len(chunk) > bleWriteChunk {
			chunk = chunk[:bleWriteChunk]
		}
		p = p[len(chunk):]

		_, err := c.rxChar.WriteWithoutResponse(chunk)
		if err != nil {
			return total - len(p), fmt.Errorf("BLE write: %w", err)
		}
	}
	return total, nil
}

// Close disconnects the BLE device.
func (c *BLEConnection) Close() error {
	c.mu.Lock()
	c.closed = true
	c.mu.Unlock()
	return c.device.Disconnect()
}
