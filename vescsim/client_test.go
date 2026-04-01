package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"net"
)

// DialTCP connects to the simulator via TCP.
func DialTCP(addr string) (net.Conn, error) {
	return net.Dial("tcp", addr)
}

// Client is a minimal VESC protocol client for testing.
type Client struct {
	conn net.Conn
}

// NewClient wraps a net.Conn with VESC protocol encoding/decoding.
func NewClient(conn net.Conn) *Client {
	return &Client{conn: conn}
}

// roundTrip sends a request and reads one response.
func (c *Client) roundTrip(payload []byte) ([]byte, error) {
	pkt, err := EncodePacket(payload)
	if err != nil {
		return nil, err
	}
	if _, err := c.conn.Write(pkt); err != nil {
		return nil, err
	}
	return DecodePacket(c.conn)
}

// FWVersionInfo holds parsed firmware version response.
type FWVersionInfo struct {
	Major           uint8
	Minor           uint8
	HWName          string
	UUID            []byte
	CustomConfigNum uint8
}

// GetFWVersion queries COMM_FW_VERSION.
func (c *Client) GetFWVersion() (*FWVersionInfo, error) {
	resp, err := c.roundTrip(BuildFWVersion())
	if err != nil {
		return nil, err
	}
	if len(resp) < 3 || CommPacketID(resp[0]) != CommFWVersion {
		return nil, fmt.Errorf("unexpected FW_VERSION response")
	}
	info := &FWVersionInfo{
		Major: resp[1],
		Minor: resp[2],
	}
	idx := 3
	info.HWName = GetString(resp, &idx)
	if idx+12 <= len(resp) {
		info.UUID = resp[idx : idx+12]
		idx += 12
	}
	// isPaired(1) + fwTestVersion(1) + hwType(1) + customConfigNum(1)
	idx += 3 // skip isPaired, fwTestVersion, hwType
	if idx < len(resp) {
		info.CustomConfigNum = resp[idx]
	}
	return info, nil
}

// GetVESCValues queries COMM_GET_VALUES.
func (c *Client) GetVESCValues() (*Values, error) {
	resp, err := c.roundTrip(BuildGetValues())
	if err != nil {
		return nil, err
	}
	if len(resp) < 2 || CommPacketID(resp[0]) != CommGetValues {
		return nil, fmt.Errorf("unexpected GET_VALUES response")
	}
	return ParseValues(resp[1:])
}

// RefloatInfo holds parsed Refloat info.
type RefloatInfo struct {
	Name    string
	Major   uint8
	Minor   uint8
	Patch   uint8
	Suffix  string
}

// GetInfo queries the Refloat COMMAND_INFO.
func (c *Client) GetInfo() (*RefloatInfo, error) {
	payload := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandInfo)}
	resp, err := c.roundTrip(payload)
	if err != nil {
		return nil, err
	}
	if len(resp) < 3 || CommPacketID(resp[0]) != CommCustomAppData {
		return nil, fmt.Errorf("unexpected CUSTOM_APP_DATA response")
	}
	if resp[1] != PackageMagic || CommandID(resp[2]) != CommandInfo {
		return nil, fmt.Errorf("not a Refloat info response")
	}
	// Skip response version (1) + flags (1)
	if len(resp) < 28 {
		return nil, fmt.Errorf("info response too short: %d", len(resp))
	}
	info := &RefloatInfo{}
	// Package name: 20 bytes starting at offset 5
	nameBytes := resp[5:25]
	info.Name = string(bytes.TrimRight(nameBytes, "\x00"))
	info.Major = resp[25]
	info.Minor = resp[26]
	info.Patch = resp[27]
	if len(resp) >= 48 {
		suffixBytes := resp[28:48]
		info.Suffix = string(bytes.TrimRight(suffixBytes, "\x00"))
	}
	return info, nil
}

// AllDataResult holds parsed GET_ALL_DATA response.
type AllDataResult struct {
	State struct {
		RunState      RunState
		StopCondition StopCondition
	}
}

// GetAllData queries Refloat COMMAND_GET_ALL_DATA with the given mode.
func (c *Client) GetAllData(mode uint8) (*AllDataResult, error) {
	payload := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandGetAllData), mode}
	resp, err := c.roundTrip(payload)
	if err != nil {
		return nil, err
	}
	if len(resp) < 3 || CommPacketID(resp[0]) != CommCustomAppData {
		return nil, fmt.Errorf("unexpected response")
	}
	if resp[1] != PackageMagic || CommandID(resp[2]) != CommandGetAllData {
		return nil, fmt.Errorf("not a GetAllData response")
	}
	// Layout after header (3 bytes): mode(1) + balance_current(2) + balance_pitch(2) + roll(2) + state_byte(1)
	if len(resp) < 11 {
		return nil, fmt.Errorf("GetAllData response too short: %d", len(resp))
	}
	result := &AllDataResult{}
	stateByte := resp[10] // offset 3+1+2+2+2 = 10
	stateNibble := stateByte & 0x0F
	result.State.RunState = decodeStateCompat(stateNibble)
	result.State.StopCondition = decodeStopConditionCompat(stateNibble)
	return result, nil
}

func decodeStopConditionCompat(v uint8) StopCondition {
	switch v {
	case 6:
		return StopPitch
	case 7:
		return StopRoll
	case 8:
		return StopSwitchHalf
	case 9:
		return StopSwitchFull
	case 12:
		return StopReverseStop
	case 13:
		return StopQuickstop
	default:
		return StopNone
	}
}

// keep import used
var _ = binary.BigEndian
