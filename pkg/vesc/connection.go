package vesc

import (
	"fmt"
	"io"
	"net"
	"sync"
	"time"
)

// Connection represents a bidirectional connection to a VESC.
type Connection struct {
	rw      io.ReadWriteCloser
	mu      sync.Mutex
	timeout time.Duration
}

// NewConnection wraps an io.ReadWriteCloser as a VESC connection.
func NewConnection(rw io.ReadWriteCloser) *Connection {
	return &Connection{
		rw:      rw,
		timeout: 2 * time.Second,
	}
}

// DialTCP connects to a VESC over TCP (useful for simulator).
func DialTCP(addr string) (*Connection, error) {
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, fmt.Errorf("dial %s: %w", addr, err)
	}
	return NewConnection(conn), nil
}

// Send encodes and sends a payload to the VESC.
func (c *Connection) Send(payload []byte) error {
	pkt, err := EncodePacket(payload)
	if err != nil {
		return err
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	_, err = c.rw.Write(pkt)
	return err
}

// Receive reads and decodes one packet from the VESC.
func (c *Connection) Receive() ([]byte, error) {
	return DecodePacket(c.rw)
}

// Request sends a command and waits for a response.
func (c *Connection) Request(payload []byte) ([]byte, error) {
	if err := c.Send(payload); err != nil {
		return nil, fmt.Errorf("send: %w", err)
	}
	resp, err := c.Receive()
	if err != nil {
		return nil, fmt.Errorf("receive: %w", err)
	}
	return resp, nil
}

// GetValues sends COMM_GET_VALUES and parses the response.
func (c *Connection) GetValues() (*Values, error) {
	resp, err := c.Request(BuildGetValues())
	if err != nil {
		return nil, err
	}
	if len(resp) < 1 || CommPacketID(resp[0]) != CommGetValues {
		return nil, fmt.Errorf("unexpected response command: 0x%02x", resp[0])
	}
	return ParseValues(resp[1:])
}

// GetFWVersion sends COMM_FW_VERSION and parses the response.
func (c *Connection) GetFWVersion() (*FWVersion, error) {
	resp, err := c.Request(BuildFWVersion())
	if err != nil {
		return nil, err
	}
	if len(resp) < 3 || CommPacketID(resp[0]) != CommFWVersion {
		return nil, fmt.Errorf("unexpected response command: 0x%02x", resp[0])
	}
	fw := &FWVersion{
		Major: resp[1],
		Minor: resp[2],
	}
	if len(resp) > 3 {
		idx := 3
		fw.HWName = GetString(resp, &idx)
		if idx+12 <= len(resp) {
			fw.UUID = make([]byte, 12)
			copy(fw.UUID, resp[idx:idx+12])
			idx += 12
		}
		if idx < len(resp) { idx++ } // isPaired
		if idx < len(resp) { idx++ } // fwTestVersion
		if idx < len(resp) { fw.HWType = HWType(resp[idx]); idx++ }
		if idx < len(resp) { fw.CustomConfigNum = resp[idx]; idx++ }
		if idx < len(resp) { idx++ } // hasPhaseFilters
		if idx < len(resp) { idx++ } // qmlHW
		if idx < len(resp) { idx++ } // qmlApp
		if idx < len(resp) { idx++ } // nrfFlags
		if idx < len(resp) { fw.PackageName = GetString(resp, &idx) }
	}
	return fw, nil
}

// SendAlive sends a keepalive packet.
func (c *Connection) SendAlive() error {
	return c.Send(BuildAlive())
}

// SendCustomAppData sends custom app data (used by Refloat).
func (c *Connection) SendCustomAppData(data []byte) error {
	return c.Send(BuildCustomAppData(data))
}

// RequestCustomAppData sends custom app data and waits for a response.
func (c *Connection) RequestCustomAppData(data []byte) ([]byte, error) {
	resp, err := c.Request(BuildCustomAppData(data))
	if err != nil {
		return nil, err
	}
	if len(resp) < 1 || CommPacketID(resp[0]) != CommCustomAppData {
		return nil, fmt.Errorf("unexpected response command: 0x%02x", resp[0])
	}
	return resp[1:], nil // Return payload without the command byte
}

// Close closes the underlying connection.
func (c *Connection) Close() error {
	return c.rw.Close()
}
