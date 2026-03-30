package simulator

import (
	"encoding/binary"
	"log"
	"math"

	"github.com/szatmary/nosedive/pkg/vesc"
)

// buildFWVersionResponse builds the full COMM_FW_VERSION response that VESC Tool expects.
// Format: [cmd][major][minor][hw_name\0][uuid:12][pairing:1][fw_test_version:1]
//         [hw_type:1][custom_config_num:1][has_phase_filters:1]
//         [qml_hw_size:4][qml_app_size:4][nrf_flags:1][fw_name\0][hw_crc:4]
func (s *Simulator) buildFWVersionResponse() []byte {
	resp := []byte{byte(vesc.CommFWVersion)}

	// Major, Minor
	resp = append(resp, s.state.FWMajor, s.state.FWMinor)

	// HW name (null-terminated)
	resp = append(resp, []byte(s.state.HWName)...)
	resp = append(resp, 0)

	// UUID (12 bytes)
	resp = append(resp, s.state.UUID[:]...)

	// isPaired (1 byte) - 0 = not paired
	resp = append(resp, 0)

	// FW test version number (1 byte) - 0 = release
	resp = append(resp, 0)

	// HW type (1 byte) - 0 = VESC
	resp = append(resp, byte(vesc.HWTypeVESC))

	// Custom config num (1 byte) - 1 = Refloat has 1 custom config
	resp = append(resp, 1)

	// Has phase filters (1 byte) - 0 = no
	resp = append(resp, 0)

	// QML HW size (4 bytes) - 0 = no QML HW UI
	resp = append(resp, 0, 0, 0, 0)

	// QML App size (4 bytes) - 0 = no QML App UI
	resp = append(resp, 0, 0, 0, 0)

	// NRF flags (1 byte) - 0 = no NRF
	resp = append(resp, 0)

	// FW name (null-terminated) - "Refloat" package name
	resp = append(resp, []byte("Refloat")...)
	resp = append(resp, 0)

	// HW CRC (4 bytes) - dummy CRC
	resp = append(resp, 0, 0, 0, 0)

	return resp
}

// buildGetValuesResponse builds the full COMM_GET_VALUES response (71 bytes).
func (s *Simulator) buildGetValuesResponse() []byte {
	resp := []byte{byte(vesc.CommGetValues)}

	resp = vesc.AppendFloat16(resp, s.state.MOSFETTemp, 10)    // temp_fet
	resp = vesc.AppendFloat16(resp, s.state.MotorTemp, 10)      // temp_motor
	resp = vesc.AppendFloat32(resp, s.state.MotorCurrent, 100)  // avg_motor_current
	resp = vesc.AppendFloat32(resp, s.state.BattCurrent, 100)   // avg_input_current
	resp = vesc.AppendFloat32(resp, 0, 100)                     // avg_id
	resp = vesc.AppendFloat32(resp, 0, 100)                     // avg_iq
	resp = vesc.AppendFloat16(resp, s.state.DutyCycle, 1000)    // duty_cycle
	resp = vesc.AppendInt32(resp, int32(s.state.ERPM))          // rpm
	resp = vesc.AppendFloat16(resp, s.state.Voltage, 10)        // input_voltage
	resp = vesc.AppendFloat32(resp, s.state.AmpHours, 10000)    // amp_hours
	resp = vesc.AppendFloat32(resp, s.state.AmpHoursCharged, 10000)
	resp = vesc.AppendFloat32(resp, s.state.WattHours, 10000)
	resp = vesc.AppendFloat32(resp, s.state.WattHoursCharged, 10000)
	resp = vesc.AppendInt32(resp, s.state.Tachometer)
	resp = vesc.AppendInt32(resp, s.state.TachometerAbs)
	resp = append(resp, byte(s.state.Fault))                    // fault_code
	resp = vesc.AppendFloat32(resp, 0, 1000000)                 // pid_pos_now
	resp = append(resp, s.state.ControllerID)                   // controller_id
	resp = vesc.AppendFloat16(resp, s.state.MOSFETTemp, 10)     // temp_mos1
	resp = vesc.AppendFloat16(resp, s.state.MOSFETTemp, 10)     // temp_mos2
	resp = vesc.AppendFloat16(resp, s.state.MOSFETTemp, 10)     // temp_mos3
	resp = vesc.AppendFloat32(resp, 0, 1000)                    // avg_vd
	resp = vesc.AppendFloat32(resp, 0, 1000)                    // avg_vq
	resp = append(resp, 0)                                      // status

	return resp
}

// buildGetValuesSetupResponse builds COMM_GET_VALUES_SETUP response.
// This is an aggregated view used by VESC Tool for multi-VESC setups.
func (s *Simulator) buildGetValuesSetupResponse() []byte {
	resp := []byte{byte(vesc.CommGetValuesSetup)}

	resp = vesc.AppendFloat16(resp, s.state.MOSFETTemp, 10)    // temp_mos
	resp = vesc.AppendFloat16(resp, s.state.MotorTemp, 10)      // temp_motor
	resp = vesc.AppendFloat32(resp, s.state.MotorCurrent, 100)  // current_motor
	resp = vesc.AppendFloat32(resp, s.state.BattCurrent, 100)   // current_in
	resp = vesc.AppendFloat16(resp, s.state.DutyCycle, 1000)    // duty
	resp = vesc.AppendFloat32(resp, s.state.ERPM, 1)            // rpm
	resp = vesc.AppendFloat32(resp, s.state.Speed, 10000)       // speed (m/s)
	resp = vesc.AppendFloat16(resp, s.state.Voltage, 10)        // v_in
	resp = vesc.AppendFloat32(resp, 0, 10000)                   // battery_wh
	resp = vesc.AppendFloat32(resp, s.state.MotorCurrent, 100)  // current_in_setup (total)
	resp = vesc.AppendFloat32(resp, s.state.MotorCurrent, 100)  // current_motor_setup
	resp = append(resp, byte(s.state.Fault))                    // fault
	resp = append(resp, 0)                                      // pid_pos
	resp = append(resp, s.state.ControllerID)                   // controller_id
	resp = append(resp, 1)                                      // num_vescs
	resp = vesc.AppendFloat32(resp, s.state.WattHours, 10000)   // wh_charge_total
	resp = vesc.AppendFloat32(resp, 0, 10000)                   // current_in_total
	resp = append(resp, 0)                                      // status (timeout_killsw)

	return resp
}

// buildPingCANResponse builds COMM_PING_CAN response.
// Returns our own controller ID to indicate we're on the bus.
func (s *Simulator) buildPingCANResponse() []byte {
	return []byte{byte(vesc.CommPingCAN), s.state.ControllerID}
}

// buildGetIMUDataResponse builds COMM_GET_IMU_DATA response.
func (s *Simulator) buildGetIMUDataResponse(payload []byte) []byte {
	resp := []byte{byte(vesc.CommGetIMUData)}

	mask := uint16(0xFFFF)
	if len(payload) > 1 {
		mask = uint16(payload[1])<<8 | uint16(payload[2])
	}

	resp = vesc.AppendUint16(resp, mask)

	// Bit 0: roll, pitch, yaw (float32 1e6)
	if mask&(1<<0) != 0 {
		resp = vesc.AppendFloat32(resp, s.state.Roll, 1000000)
		resp = vesc.AppendFloat32(resp, s.state.Pitch, 1000000)
		resp = vesc.AppendFloat32(resp, 0, 1000000) // yaw
	}
	// Bit 1: accel x, y, z
	if mask&(1<<1) != 0 {
		resp = vesc.AppendFloat32(resp, 0, 1000000) // accel_x
		resp = vesc.AppendFloat32(resp, 0, 1000000) // accel_y
		resp = vesc.AppendFloat32(resp, -9.81, 1000000) // accel_z (gravity)
	}
	// Bit 2: gyro x, y, z
	if mask&(1<<2) != 0 {
		resp = vesc.AppendFloat32(resp, 0, 1000000) // gyro_x
		resp = vesc.AppendFloat32(resp, 0, 1000000) // gyro_y
		resp = vesc.AppendFloat32(resp, 0, 1000000) // gyro_z
	}
	// Bit 3: mag x, y, z
	if mask&(1<<3) != 0 {
		resp = vesc.AppendFloat32(resp, 0, 1000000) // mag_x
		resp = vesc.AppendFloat32(resp, 0, 1000000) // mag_y
		resp = vesc.AppendFloat32(resp, 0, 1000000) // mag_z
	}
	// Bit 4: quaternion w, x, y, z
	if mask&(1<<4) != 0 {
		// Convert pitch/roll to quaternion (simplified)
		pitchRad := s.state.Pitch * math.Pi / 180
		rollRad := s.state.Roll * math.Pi / 180
		cy := 1.0 // cos(yaw/2) = 1 for yaw=0
		sy := 0.0
		cp := math.Cos(pitchRad / 2)
		sp := math.Sin(pitchRad / 2)
		cr := math.Cos(rollRad / 2)
		sr := math.Sin(rollRad / 2)
		qw := cr*cp*cy + sr*sp*sy
		qx := sr*cp*cy - cr*sp*sy
		qy := cr*sp*cy + sr*cp*sy
		qz := cr*cp*sy - sr*sp*cy
		resp = vesc.AppendFloat32(resp, qw, 1000000)
		resp = vesc.AppendFloat32(resp, qx, 1000000)
		resp = vesc.AppendFloat32(resp, qy, 1000000)
		resp = vesc.AppendFloat32(resp, qz, 1000000)
	}

	return resp
}

// buildGetDecodedADCResponse builds COMM_GET_DECODED_ADC response.
func (s *Simulator) buildGetDecodedADCResponse() []byte {
	resp := []byte{byte(vesc.CommGetDecodedADC)}
	resp = vesc.AppendFloat32(resp, s.state.ADC1, 1000000) // decoded value
	resp = vesc.AppendFloat32(resp, s.state.ADC1, 1000000) // voltage
	resp = vesc.AppendFloat32(resp, s.state.ADC2, 1000000) // decoded value 2
	resp = vesc.AppendFloat32(resp, s.state.ADC2, 1000000) // voltage 2
	return resp
}

// buildGetDecodedPPMResponse builds COMM_GET_DECODED_PPM response.
func (s *Simulator) buildGetDecodedPPMResponse() []byte {
	resp := []byte{byte(vesc.CommGetDecodedPPM)}
	resp = vesc.AppendFloat32(resp, 0, 1000000) // decoded value
	resp = vesc.AppendFloat32(resp, 0, 1000000) // pulse length
	return resp
}

// buildGetDecodedChukResponse builds COMM_GET_DECODED_CHUK response.
func (s *Simulator) buildGetDecodedChukResponse() []byte {
	resp := []byte{byte(vesc.CommGetDecodedChuk)}
	resp = vesc.AppendFloat32(resp, 0, 1000000)
	return resp
}

// buildTerminalResponse handles terminal commands by echoing back.
func (s *Simulator) buildTerminalResponse(payload []byte) []byte {
	resp := []byte{byte(vesc.CommPrintText)}
	msg := "NoseDive Simulator v1.0\n"
	if len(payload) > 1 {
		cmd := string(payload[1:])
		switch cmd {
		case "help":
			msg = "Available commands:\n  help - Show this help\n  faults - Show fault log\n  status - Show status\n"
		case "faults":
			msg = "No faults logged.\n"
		case "status":
			msg = "Simulator running. State: " + s.state.RunState.String() + "\n"
		default:
			msg = "Unknown command: " + cmd + "\n"
		}
	}
	resp = append(resp, []byte(msg)...)
	resp = append(resp, 0)
	return resp
}

// buildGetCustomConfigXMLResponse handles chunked XML config download.
// Request format: [cmd][confInd:1][len:4][offset:4]
// Response format: [cmd][confInd:1][totalSize:4][offset:4][data...]
func (s *Simulator) buildGetCustomConfigXMLResponse(payload []byte) []byte {
	if len(payload) < 10 {
		return nil
	}
	idx := 1
	confInd := payload[idx]
	idx++
	_ = confInd // we only have one config (index 0)

	requestLen := int(binary.BigEndian.Uint32(payload[idx:]))
	idx += 4
	offset := int(binary.BigEndian.Uint32(payload[idx:]))

	xmlData := s.state.ConfigXML
	totalSize := len(xmlData)

	// Calculate chunk
	end := offset + requestLen
	if end > totalSize {
		end = totalSize
	}
	if offset > totalSize {
		offset = totalSize
	}
	chunk := xmlData[offset:end]

	resp := []byte{byte(vesc.CommGetCustomConfigXML)}
	resp = append(resp, confInd)
	resp = vesc.AppendInt32(resp, int32(totalSize))
	resp = vesc.AppendInt32(resp, int32(offset))
	resp = append(resp, chunk...)

	return resp
}

// buildGetCustomConfigResponse returns the serialized binary config.
// Request format: [cmd][confInd:1]
// Response format: [cmd][confInd:1][serialized config data...]
func (s *Simulator) buildGetCustomConfigResponse(payload []byte) []byte {
	confInd := uint8(0)
	if len(payload) > 1 {
		confInd = payload[1]
	}

	resp := []byte{byte(vesc.CommGetCustomConfig)}
	resp = append(resp, confInd)
	resp = append(resp, s.state.ConfigData...)

	return resp
}

// buildSetCustomConfigResponse handles config writes.
// Request format: [cmd][confInd:1][serialized config data...]
// Response format: [cmd][confInd:1]
func (s *Simulator) buildSetCustomConfigResponse(payload []byte) []byte {
	confInd := uint8(0)
	if len(payload) > 1 {
		confInd = payload[1]
	}
	if len(payload) > 2 {
		s.state.ConfigData = make([]byte, len(payload)-2)
		copy(s.state.ConfigData, payload[2:])
		log.Printf("simulator: custom config %d updated (%d bytes)", confInd, len(s.state.ConfigData))
	}
	return []byte{byte(vesc.CommSetCustomConfig), confInd}
}

// buildGetMCConfResponse returns a minimal motor configuration.
// The real config is ~400+ bytes of serialized parameters.
// We return a realistic default for a typical FOC setup.
func (s *Simulator) buildGetMCConfResponse() []byte {
	resp := []byte{byte(vesc.CommGetMCConf)}
	resp = append(resp, s.state.MCConf...)
	return resp
}

// buildGetAppConfResponse returns a minimal app configuration.
func (s *Simulator) buildGetAppConfResponse() []byte {
	resp := []byte{byte(vesc.CommGetAppConf)}
	resp = append(resp, s.state.AppConf...)
	return resp
}

// buildGetBatteryCutResponse returns battery cutoff values.
func (s *Simulator) buildGetBatteryCutResponse() []byte {
	resp := []byte{byte(vesc.CommGetBatteryCut)}
	resp = vesc.AppendFloat32(resp, 42.0, 1000) // start voltage
	resp = vesc.AppendFloat32(resp, 40.0, 1000) // end voltage
	return resp
}

// buildGetStatsResponse returns usage statistics.
func (s *Simulator) buildGetStatsResponse(payload []byte) []byte {
	resp := []byte{byte(vesc.CommGetStats)}

	mask := uint16(0xFFFF)
	if len(payload) > 2 {
		mask = uint16(payload[1])<<8 | uint16(payload[2])
	}
	resp = vesc.AppendUint16(resp, uint16(mask))

	// Bit 0: speed_total (float32_auto)
	if mask&(1<<0) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 1: speed_count (float32_auto)
	if mask&(1<<1) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 2: distance_total (float32_auto)
	if mask&(1<<2) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 3: current_total (float32_auto)
	if mask&(1<<3) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 4: charge_total (float32_auto)
	if mask&(1<<4) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 5: watt_hours (float32_auto)
	if mask&(1<<5) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 6: watt_hours_charged (float32_auto)
	if mask&(1<<6) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 7: count_time (float32_auto)
	if mask&(1<<7) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}
	// Bit 8: temp_motor_avg (float32_auto)
	if mask&(1<<8) != 0 {
		resp = appendFloat32Auto(resp, s.state.MotorTemp)
	}
	// Bit 9: temp_motor_max (float32_auto)
	if mask&(1<<9) != 0 {
		resp = appendFloat32Auto(resp, s.state.MotorTemp)
	}
	// Bit 10: count_time (float32_auto)
	if mask&(1<<10) != 0 {
		resp = appendFloat32Auto(resp, 0)
	}

	return resp
}

// buildQMLUIResponse returns empty QML UI data.
func (s *Simulator) buildQMLUIResponse(cmd vesc.CommPacketID, payload []byte) []byte {
	resp := []byte{byte(cmd)}
	resp = vesc.AppendInt32(resp, 0) // total size = 0 (no QML)
	resp = vesc.AppendInt32(resp, 0) // offset = 0
	return resp
}
