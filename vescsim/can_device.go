package main

import (
	"log"

)

// CANDevice represents a device on the simulated CAN bus that can respond to
// VESC commands forwarded via COMM_FORWARD_CAN.
type CANDevice interface {
	// ID returns the CAN controller ID for this device.
	ID() uint8
	// HandleCommand processes a VESC command payload and returns the response.
	HandleCommand(payload []byte) []byte
}

// VESCExpress simulates a VESC Express module on the CAN bus.
// The Express is an ESP32-based BLE/WiFi bridge. It has no motor controller —
// it serves firmware version info, manages BLE/WiFi, and forwards commands
// to the main VESC over CAN.
type VESCExpress struct {
	ControllerID uint8
	FWMajor      uint8
	FWMinor      uint8
	HWName       string
	UUID         [12]byte
}

// DefaultVESCExpress returns a VESC Express with typical defaults.
// Controller ID 253 is the conventional Express address.
func DefaultVESCExpress() *VESCExpress {
	ve := &VESCExpress{
		ControllerID: 253,
		FWMajor:      6,
		FWMinor:      5,
		HWName:       "VESC Express T",
	}
	copy(ve.UUID[:], []byte{0x45, 0x58, 0x50, 0x52, 0x45, 0x53, 0x53, 0x53, 0x49, 0x4d, 0x30, 0x31})
	return ve
}

func (ve *VESCExpress) ID() uint8 { return ve.ControllerID }

func (ve *VESCExpress) HandleCommand(payload []byte) []byte {
	if len(payload) == 0 {
		return nil
	}
	cmd := CommPacketID(payload[0])

	switch cmd {
	case CommFWVersion:
		return ve.buildFWVersionResponse()
	case CommGetValues:
		return ve.buildGetValuesResponse()
	case CommPingCAN:
		return []byte{byte(CommPingCAN), ve.ControllerID}
	case CommAlive:
		return nil
	case CommGetIMUData:
		// Express has no IMU — return minimal response
		resp := []byte{byte(CommGetIMUData)}
		resp = AppendUint16(resp, 0) // empty mask
		return resp
	case CommGetCustomConfigXML:
		// No custom config on Express
		if len(payload) < 10 {
			return nil
		}
		confInd := payload[1]
		resp := []byte{byte(CommGetCustomConfigXML)}
		resp = append(resp, confInd)
		resp = AppendInt32(resp, 0) // total size = 0
		resp = AppendInt32(resp, 0) // offset = 0
		return resp
	case CommGetMCConf, CommGetMCConfDefault:
		// Express has no motor — return minimal config
		return []byte{byte(CommGetMCConf)}
	case CommGetAppConf, CommGetAppConfDefault:
		return []byte{byte(CommGetAppConf)}
	case CommReboot, CommShutdown:
		log.Printf("vesc_express [%d]: received command 0x%02x (ignored)", ve.ControllerID, cmd)
		return nil
	case CommTerminalCmd, CommTerminalCmdSync:
		resp := []byte{byte(CommPrintText)}
		resp = append(resp, []byte("VESC Express (simulated)\n")...)
		resp = append(resp, 0)
		return resp
	default:
		log.Printf("vesc_express [%d]: unhandled command 0x%02x", ve.ControllerID, cmd)
		return nil
	}
}

// buildFWVersionResponse builds the COMM_FW_VERSION response for a VESC Express.
// Same wire format as a regular VESC but with HWType = VESCExpress (3) and
// no custom configs, no phase filters, no QML.
func (ve *VESCExpress) buildFWVersionResponse() []byte {
	resp := []byte{byte(CommFWVersion)}

	resp = append(resp, ve.FWMajor, ve.FWMinor)

	// HW name (null-terminated)
	resp = append(resp, []byte(ve.HWName)...)
	resp = append(resp, 0)

	// UUID (12 bytes)
	resp = append(resp, ve.UUID[:]...)

	// isPaired
	resp = append(resp, 0)
	// FW test version
	resp = append(resp, 0)
	// HW type = VESC Express
	resp = append(resp, byte(HWTypeVESCExpress))
	// Custom config num = 0 (Express has no custom app)
	resp = append(resp, 0)
	// Has phase filters = 0
	resp = append(resp, 0)
	// QML HW = 0
	resp = append(resp, 0)
	// QML App = 0
	resp = append(resp, 0)
	// NRF flags (BLE name + pin supported)
	resp = append(resp, 0x03)
	// FW name (null-terminated)
	resp = append(resp, []byte("VESC Express")...)
	resp = append(resp, 0)
	// HW config CRC
	resp = AppendUint32(resp, 0xCAFEBEEF)

	return resp
}

// VESCBMS simulates a BMS (Battery Management System) on the CAN bus.
// Typical CAN ID is 10. Reports cell voltages, temperatures, and SOC.
type VESCBMS struct {
	ControllerID   uint8
	FWMajor        uint8
	FWMinor        uint8
	HWName         string
	UUID           [12]byte
	CellCount      int
	CellVoltage    float64 // per-cell voltage
	Temperature    float64 // pack temp celsius
	SOC            float64 // 0-100
	TotalVoltage   float64
	CurrentIn      float64
}

// DefaultVESCBMS returns a BMS with typical defaults for a 20S battery.
func DefaultVESCBMS() *VESCBMS {
	bms := &VESCBMS{
		ControllerID: 10,
		FWMajor:      6,
		FWMinor:      5,
		HWName:       "VESC BMS",
		CellCount:    20,
		CellVoltage:  4.15, // fully charged
		Temperature:  28.0,
		SOC:          95.0,
		TotalVoltage: 83.0, // 20 * 4.15
		CurrentIn:    0,
	}
	copy(bms.UUID[:], []byte{0x42, 0x4d, 0x53, 0x53, 0x49, 0x4d, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30})
	return bms
}

func (b *VESCBMS) ID() uint8 { return b.ControllerID }

func (b *VESCBMS) HandleCommand(payload []byte) []byte {
	if len(payload) == 0 {
		return nil
	}
	cmd := CommPacketID(payload[0])

	switch cmd {
	case CommFWVersion:
		return b.buildFWVersionResponse()
	case CommGetValues:
		return b.buildGetValuesResponse()
	case CommBMSGetValues:
		return b.buildBMSValuesResponse()
	case CommPingCAN:
		return []byte{byte(CommPingCAN), b.ControllerID}
	case CommAlive:
		return nil
	case CommGetMCConf, CommGetMCConfDefault:
		return []byte{byte(CommGetMCConf)}
	case CommGetAppConf, CommGetAppConfDefault:
		return []byte{byte(CommGetAppConf)}
	case CommReboot, CommShutdown:
		log.Printf("vesc_bms [%d]: received command 0x%02x (ignored)", b.ControllerID, cmd)
		return nil
	case CommTerminalCmd, CommTerminalCmdSync:
		resp := []byte{byte(CommPrintText)}
		resp = append(resp, []byte("VESC BMS (simulated)\n")...)
		resp = append(resp, 0)
		return resp
	default:
		log.Printf("vesc_bms [%d]: unhandled command 0x%02x", b.ControllerID, cmd)
		return nil
	}
}

func (b *VESCBMS) buildFWVersionResponse() []byte {
	resp := []byte{byte(CommFWVersion)}
	resp = append(resp, b.FWMajor, b.FWMinor)
	resp = append(resp, []byte(b.HWName)...)
	resp = append(resp, 0)
	resp = append(resp, b.UUID[:]...)
	resp = append(resp, 0)    // isPaired
	resp = append(resp, 0)    // FW test version
	resp = append(resp, byte(HWTypeVESC)) // BMS uses standard VESC type
	resp = append(resp, 0)    // custom config num
	resp = append(resp, 0)    // has phase filters
	resp = append(resp, 0)    // QML HW
	resp = append(resp, 0)    // QML App
	resp = append(resp, 0)    // NRF flags
	resp = append(resp, []byte("VESC BMS")...)
	resp = append(resp, 0)
	resp = AppendUint32(resp, 0xBAADF00D)
	return resp
}

func (b *VESCBMS) buildGetValuesResponse() []byte {
	resp := []byte{byte(CommGetValues)}
	resp = AppendFloat16(resp, b.Temperature, 10) // temp_fet (board temp)
	resp = AppendFloat16(resp, 0, 10)              // temp_motor (N/A)
	resp = AppendFloat32(resp, 0, 100)              // avg_motor_current
	resp = AppendFloat32(resp, b.CurrentIn, 100)    // avg_input_current
	resp = AppendFloat32(resp, 0, 100)              // avg_id
	resp = AppendFloat32(resp, 0, 100)              // avg_iq
	resp = AppendFloat16(resp, 0, 1000)             // duty_cycle
	resp = AppendFloat32(resp, 0, 1)                // rpm
	resp = AppendFloat16(resp, b.TotalVoltage, 10)  // input_voltage
	resp = AppendFloat32(resp, 0, 10000)            // amp_hours
	resp = AppendFloat32(resp, 0, 10000)            // amp_hours_charged
	resp = AppendFloat32(resp, 0, 10000)            // watt_hours
	resp = AppendFloat32(resp, 0, 10000)            // watt_hours_charged
	resp = AppendInt32(resp, 0)                      // tachometer
	resp = AppendInt32(resp, 0)                      // tachometer_abs
	resp = append(resp, 0)                                // fault
	resp = AppendFloat32(resp, 0, 1000000)          // pid_pos_now
	resp = append(resp, b.ControllerID)                   // controller_id
	resp = AppendFloat16(resp, b.Temperature, 10)   // temp_mos1
	resp = AppendFloat16(resp, b.Temperature, 10)   // temp_mos2
	resp = AppendFloat16(resp, b.Temperature, 10)   // temp_mos3
	resp = AppendFloat32(resp, 0, 1000)             // avg_vd
	resp = AppendFloat32(resp, 0, 1000)             // avg_vq
	resp = append(resp, 0)                                // status
	return resp
}

// buildBMSValuesResponse returns COMM_BMS_GET_VALUES with cell data.
func (b *VESCBMS) buildBMSValuesResponse() []byte {
	resp := []byte{byte(CommBMSGetValues)}

	// Pack voltage
	resp = AppendFloat32(resp, b.TotalVoltage, 1e6)
	// Pack current
	resp = AppendFloat32(resp, b.CurrentIn, 1e6)
	// SOC (0-1)
	resp = AppendFloat32(resp, b.SOC/100.0, 1e6)

	// Cell count
	resp = append(resp, byte(b.CellCount))
	// Cell voltages
	for i := 0; i < b.CellCount; i++ {
		// Add slight variation between cells
		v := b.CellVoltage + float64(i%3)*0.005 - 0.005
		resp = AppendFloat16(resp, v, 1000)
	}

	// Cell balancing bitmap (uint64 as 8 bytes, all off)
	for i := 0; i < 8; i++ {
		resp = append(resp, 0)
	}

	// Temp sensor count
	resp = append(resp, 3)
	// Temperatures
	resp = AppendFloat16(resp, b.Temperature, 100)
	resp = AppendFloat16(resp, b.Temperature+1.5, 100)
	resp = AppendFloat16(resp, b.Temperature-0.5, 100)

	// Humidity
	resp = AppendFloat16(resp, 35.0, 100)

	return resp
}

// buildGetValuesResponse returns a minimal COMM_GET_VALUES for the Express.
// The Express has no motor so most values are zero. It reports its own
// controller ID and the shared bus voltage.
func (ve *VESCExpress) buildGetValuesResponse() []byte {
	resp := []byte{byte(CommGetValues)}

	resp = AppendFloat16(resp, 32.0, 10)  // temp_fet (ESP32 temp)
	resp = AppendFloat16(resp, 0, 10)      // temp_motor (N/A)
	resp = AppendFloat32(resp, 0, 100)      // avg_motor_current
	resp = AppendFloat32(resp, 0, 100)      // avg_input_current
	resp = AppendFloat32(resp, 0, 100)      // avg_id
	resp = AppendFloat32(resp, 0, 100)      // avg_iq
	resp = AppendFloat16(resp, 0, 1000)     // duty_cycle
	resp = AppendFloat32(resp, 0, 1)        // rpm
	resp = AppendFloat16(resp, 63.0, 10)    // input_voltage (bus voltage)
	resp = AppendFloat32(resp, 0, 10000)    // amp_hours
	resp = AppendFloat32(resp, 0, 10000)    // amp_hours_charged
	resp = AppendFloat32(resp, 0, 10000)    // watt_hours
	resp = AppendFloat32(resp, 0, 10000)    // watt_hours_charged
	resp = AppendInt32(resp, 0)              // tachometer
	resp = AppendInt32(resp, 0)              // tachometer_abs
	resp = append(resp, 0)                        // fault_code = none
	resp = AppendFloat32(resp, 0, 1000000)  // pid_pos_now
	resp = append(resp, ve.ControllerID)          // controller_id
	resp = AppendFloat16(resp, 32.0, 10)    // temp_mos1
	resp = AppendFloat16(resp, 32.0, 10)    // temp_mos2
	resp = AppendFloat16(resp, 32.0, 10)    // temp_mos3
	resp = AppendFloat32(resp, 0, 1000)     // avg_vd
	resp = AppendFloat32(resp, 0, 1000)     // avg_vq
	resp = append(resp, 0)                        // status

	return resp
}
