package refloat

import (
	"fmt"

	"github.com/szatmary/nosedive/pkg/vesc"
)

// Client provides high-level access to Refloat commands over a VESC connection.
type Client struct {
	conn *vesc.Connection
}

// NewClient creates a Refloat client using an existing VESC connection.
func NewClient(conn *vesc.Connection) *Client {
	return &Client{conn: conn}
}

func (c *Client) sendCommand(cmd CommandID, payload ...byte) ([]byte, error) {
	data := make([]byte, 2+len(payload))
	data[0] = PackageMagic
	data[1] = byte(cmd)
	copy(data[2:], payload)

	resp, err := c.conn.RequestCustomAppData(data)
	if err != nil {
		return nil, err
	}
	if len(resp) < 2 {
		return nil, fmt.Errorf("response too short: %d bytes", len(resp))
	}
	if resp[0] != PackageMagic {
		return nil, fmt.Errorf("bad magic: got %d, want %d", resp[0], PackageMagic)
	}
	return resp[2:], nil // skip magic + command
}

// GetInfo requests package version info (COMMAND_INFO version 2).
func (c *Client) GetInfo() (*PackageInfo, error) {
	data, err := c.sendCommand(CommandInfo, 2) // request version 2 format
	if err != nil {
		return nil, err
	}
	if len(data) < 50 {
		return nil, fmt.Errorf("info response too short: %d bytes", len(data))
	}

	info := &PackageInfo{}
	// Skip version byte (1) and flags byte (1)
	idx := 2
	// Name: fixed 20 bytes
	nameEnd := idx + 20
	info.Name = trimNull(string(data[idx:nameEnd]))
	idx = nameEnd
	info.MajorVersion = data[idx]
	idx++
	info.MinorVersion = data[idx]
	idx++
	info.PatchVersion = data[idx]
	idx++
	// Suffix: fixed 20 bytes
	suffixEnd := idx + 20
	info.Suffix = trimNull(string(data[idx:suffixEnd]))

	return info, nil
}

// GetAllData requests compact real-time data (COMMAND_GET_ALLDATA).
// mode: 0-4, higher modes include more data fields.
func (c *Client) GetAllData(mode uint8) (*RTData, error) {
	data, err := c.sendCommand(CommandGetAllData, mode)
	if err != nil {
		return nil, err
	}
	return parseAllData(data, mode)
}

// GetRTData requests legacy real-time data (COMMAND_GET_RTDATA).
func (c *Client) GetRTData() (*RTData, error) {
	data, err := c.sendCommand(CommandGetRTData)
	if err != nil {
		return nil, err
	}
	return parseRTData(data)
}

// SendAlive sends a keepalive to the VESC.
func (c *Client) SendAlive() error {
	return c.conn.SendAlive()
}

// GetVESCValues gets raw VESC values.
func (c *Client) GetVESCValues() (*vesc.Values, error) {
	return c.conn.GetValues()
}

// GetFWVersion gets VESC firmware version.
func (c *Client) GetFWVersion() (*vesc.FWVersion, error) {
	return c.conn.GetFWVersion()
}

// Close closes the underlying connection.
func (c *Client) Close() error {
	return c.conn.Close()
}

func trimNull(s string) string {
	for i, c := range s {
		if c == 0 {
			return s[:i]
		}
	}
	return s
}
