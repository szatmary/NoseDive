package main

import (
	"encoding/binary"
	"log"
	"math"

)

// buildFWVersionResponse builds the full COMM_FW_VERSION response that VESC Tool expects.
// Format: [cmd][major][minor][hw_name\0][uuid:12][pairing:1][fw_test_version:1]
//         [hw_type:1][custom_config_num:1][has_phase_filters:1]
//         [qml_hw_size:4][qml_app_size:4][nrf_flags:1][fw_name\0][hw_crc:4]
func (s *Simulator) buildFWVersionResponse() []byte {
	resp := []byte{byte(CommFWVersion)}

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
	resp = append(resp, byte(HWTypeVESC))

	// Custom config num (1 byte) - 1 if Refloat is installed
	if s.state.HasRefloat {
		resp = append(resp, 1)
	} else {
		resp = append(resp, 0)
	}

	// Has phase filters (1 byte) - 0 = no
	resp = append(resp, 0)

	// QML HW (1 byte) - 0=none, 1=has, 2=fullscreen
	resp = append(resp, 0)

	// QML App (1 byte) - 1=has Refloat QML UI
	if s.state.HasRefloat && len(compressedRefloatQML) > 0 {
		resp = append(resp, 1)
	} else {
		resp = append(resp, 0)
	}

	// NRF flags (1 byte) - bit0=nameSupported, bit1=pinSupported
	resp = append(resp, 0)

	// FW name (null-terminated) - package name if installed
	if s.state.HasRefloat {
		resp = append(resp, []byte(s.state.PkgName)...)
	}
	resp = append(resp, 0)

	// HW config CRC (4 bytes) - used for caching
	resp = AppendUint32(resp, MCConfSignature605+2) // bust VESC Tool cache

	return resp
}

// buildGetValuesResponse builds the full COMM_GET_VALUES response (71 bytes).
func (s *Simulator) buildGetValuesResponse() []byte {
	resp := []byte{byte(CommGetValues)}

	resp = AppendFloat16(resp, s.state.MOSFETTemp, 10)    // temp_fet
	resp = AppendFloat16(resp, s.state.MotorTemp, 10)      // temp_motor
	resp = AppendFloat32(resp, s.state.MotorCurrent, 100)  // avg_motor_current
	resp = AppendFloat32(resp, s.state.BattCurrent, 100)   // avg_input_current
	resp = AppendFloat32(resp, 0, 100)                     // avg_id
	resp = AppendFloat32(resp, 0, 100)                     // avg_iq
	resp = AppendFloat16(resp, s.state.DutyCycle, 1000)    // duty_cycle
	resp = AppendFloat32(resp, s.state.ERPM, 1)           // rpm (float32, scale=1)
	resp = AppendFloat16(resp, s.state.Voltage, 10)        // input_voltage
	resp = AppendFloat32(resp, s.state.AmpHours, 10000)    // amp_hours
	resp = AppendFloat32(resp, s.state.AmpHoursCharged, 10000)
	resp = AppendFloat32(resp, s.state.WattHours, 10000)
	resp = AppendFloat32(resp, s.state.WattHoursCharged, 10000)
	resp = AppendInt32(resp, s.state.Tachometer)
	resp = AppendInt32(resp, s.state.TachometerAbs)
	resp = append(resp, byte(s.state.Fault))                    // fault_code
	resp = AppendFloat32(resp, 0, 1000000)                 // pid_pos_now
	resp = append(resp, s.state.ControllerID)                   // controller_id
	resp = AppendFloat16(resp, s.state.MOSFETTemp, 10)     // temp_mos1
	resp = AppendFloat16(resp, s.state.MOSFETTemp, 10)     // temp_mos2
	resp = AppendFloat16(resp, s.state.MOSFETTemp, 10)     // temp_mos3
	resp = AppendFloat32(resp, 0, 1000)                    // avg_vd
	resp = AppendFloat32(resp, 0, 1000)                    // avg_vq
	resp = append(resp, 0)                                      // status

	return resp
}

// buildGetValuesSetupResponse builds COMM_GET_VALUES_SETUP response.
// Format from datatypes.h / commands.c for multi-VESC aggregated view.
func (s *Simulator) buildGetValuesSetupResponse() []byte {
	resp := []byte{byte(CommGetValuesSetup)}

	resp = AppendFloat16(resp, s.state.MOSFETTemp, 10)        // temp_mos
	resp = AppendFloat16(resp, s.state.MotorTemp, 10)          // temp_motor
	resp = AppendFloat32(resp, s.state.MotorCurrent, 100)      // current_tot
	resp = AppendFloat32(resp, s.state.BattCurrent, 100)       // current_in_tot
	resp = AppendFloat16(resp, s.state.DutyCycle, 1000)        // duty_cycle
	resp = AppendFloat32(resp, s.state.ERPM, 1)                // rpm
	resp = AppendFloat32(resp, s.state.Speed, 1000)            // speed (m/s, scale=1e3)
	resp = AppendFloat16(resp, s.state.Voltage, 10)            // input_voltage
	resp = AppendFloat16(resp, s.state.BatteryPercent(), 1000)     // battery_level (0-1)
	resp = AppendFloat32(resp, s.state.AmpHours, 10000)           // ah_tot
	resp = AppendFloat32(resp, s.state.AmpHoursCharged, 10000)    // ah_charge_tot
	resp = AppendFloat32(resp, s.state.WattHours, 10000)          // wh_tot
	resp = AppendFloat32(resp, s.state.WattHoursCharged, 10000)   // wh_charge_tot
	resp = AppendFloat32(resp, s.state.Distance, 1000)            // distance (meters)
	resp = AppendFloat32(resp, s.state.Distance, 1000)            // distance_abs
	resp = AppendFloat32(resp, 0, 1000000)                        // pid_pos_now
	resp = append(resp, byte(s.state.Fault))                            // fault_code
	resp = append(resp, s.state.ControllerID)                           // controller_id
	resp = append(resp, 1)                                              // num_vescs
	whLeft := s.state.BatteryPercent() * 720.0                          // estimated Wh remaining
	resp = AppendFloat32(resp, whLeft, 1000)                       // wh_batt_left
	resp = AppendUint32(resp, uint32(s.state.Odometer))            // odometer (meters)
	resp = AppendUint32(resp, s.state.UptimeMs)                    // uptime_ms

	return resp
}

// buildPingCANResponse builds COMM_PING_CAN response.
// Returns all device IDs on the CAN bus: ourselves plus every CAN device.
func (s *Simulator) buildPingCANResponse() []byte {
	resp := []byte{byte(CommPingCAN), s.state.ControllerID}
	for _, dev := range s.canDevices {
		resp = append(resp, dev.ID())
	}
	return resp
}

// buildGetIMUDataResponse builds COMM_GET_IMU_DATA response.
func (s *Simulator) buildGetIMUDataResponse(payload []byte) []byte {
	resp := []byte{byte(CommGetIMUData)}

	mask := uint16(0xFFFF)
	if len(payload) > 1 {
		mask = uint16(payload[1])<<8 | uint16(payload[2])
	}

	resp = AppendUint16(resp, mask)

	// Bit 0: roll, pitch, yaw (float32 1e6)
	if mask&(1<<0) != 0 {
		resp = AppendFloat32(resp, s.state.Roll, 1000000)
		resp = AppendFloat32(resp, s.state.Pitch, 1000000)
		resp = AppendFloat32(resp, 0, 1000000) // yaw
	}
	// Bit 1: accel x, y, z (from tilt angles + gravity)
	if mask&(1<<1) != 0 {
		pitchRad := s.state.Pitch * math.Pi / 180
		rollRad := s.state.Roll * math.Pi / 180
		// Gravity projected through tilt
		ax := -9.81 * math.Sin(pitchRad)
		ay := 9.81 * math.Sin(rollRad) * math.Cos(pitchRad)
		az := -9.81 * math.Cos(pitchRad) * math.Cos(rollRad)
		resp = AppendFloat32(resp, ax, 1000000)
		resp = AppendFloat32(resp, ay, 1000000)
		resp = AppendFloat32(resp, az, 1000000)
	}
	// Bit 2: gyro x, y, z (deg/s from pitch/roll rate of change)
	if mask&(1<<2) != 0 {
		gyroPitch := (s.state.Pitch - s.state.PrevPitch) / 0.02  // deg/s
		gyroRoll := (s.state.Roll - s.state.PrevRoll) / 0.02
		resp = AppendFloat32(resp, gyroRoll, 1000000)   // gyro_x = roll rate
		resp = AppendFloat32(resp, gyroPitch, 1000000)  // gyro_y = pitch rate
		resp = AppendFloat32(resp, 0, 1000000)          // gyro_z = yaw rate
	}
	// Bit 3: mag x, y, z (simulated compass heading — roughly north)
	if mask&(1<<3) != 0 {
		resp = AppendFloat32(resp, 0.25, 1000000)  // mag_x (Gauss)
		resp = AppendFloat32(resp, 0.0, 1000000)   // mag_y
		resp = AppendFloat32(resp, -0.45, 1000000) // mag_z
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
		resp = AppendFloat32(resp, qw, 1000000)
		resp = AppendFloat32(resp, qx, 1000000)
		resp = AppendFloat32(resp, qy, 1000000)
		resp = AppendFloat32(resp, qz, 1000000)
	}

	return resp
}

// buildGetDecodedADCResponse builds COMM_GET_DECODED_ADC response.
func (s *Simulator) buildGetDecodedADCResponse() []byte {
	resp := []byte{byte(CommGetDecodedADC)}
	resp = AppendFloat32(resp, s.state.ADC1, 1000000) // decoded value
	resp = AppendFloat32(resp, s.state.ADC1, 1000000) // voltage
	resp = AppendFloat32(resp, s.state.ADC2, 1000000) // decoded value 2
	resp = AppendFloat32(resp, s.state.ADC2, 1000000) // voltage 2
	return resp
}

// buildGetDecodedPPMResponse builds COMM_GET_DECODED_PPM response.
func (s *Simulator) buildGetDecodedPPMResponse() []byte {
	resp := []byte{byte(CommGetDecodedPPM)}
	resp = AppendFloat32(resp, 0, 1000000) // decoded value
	resp = AppendFloat32(resp, 0, 1000000) // pulse length
	return resp
}

// buildGetDecodedChukResponse builds COMM_GET_DECODED_CHUK response.
func (s *Simulator) buildGetDecodedChukResponse() []byte {
	resp := []byte{byte(CommGetDecodedChuk)}
	resp = AppendFloat32(resp, 0, 1000000)
	return resp
}

// buildTerminalResponse handles terminal commands by echoing back.
func (s *Simulator) buildTerminalResponse(payload []byte) []byte {
	resp := []byte{byte(CommPrintText)}
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

	resp := []byte{byte(CommGetCustomConfigXML)}
	resp = append(resp, confInd)
	resp = AppendInt32(resp, int32(totalSize))
	resp = AppendInt32(resp, int32(offset))
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

	resp := []byte{byte(CommGetCustomConfig)}
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
	return []byte{byte(CommSetCustomConfig), confInd}
}

// buildGetMCConfResponse returns a minimal motor configuration.
// The real config is ~400+ bytes of serialized parameters.
// We return a realistic default for a typical FOC setup.
func (s *Simulator) buildGetMCConfResponse() []byte {
	resp := []byte{byte(CommGetMCConf)}
	resp = append(resp, s.state.MCConf...)
	return resp
}

// buildGetAppConfResponse returns a minimal app configuration.
func (s *Simulator) buildGetAppConfResponse() []byte {
	resp := []byte{byte(CommGetAppConf)}
	resp = append(resp, s.state.AppConf...)
	return resp
}

// buildGetBatteryCutResponse returns battery cutoff values.
func (s *Simulator) buildGetBatteryCutResponse() []byte {
	resp := []byte{byte(CommGetBatteryCut)}
	// Start cutoff slightly above min, end at min
	cutStart := s.state.BatteryVoltageMin + 2.0
	cutEnd := s.state.BatteryVoltageMin
	resp = AppendFloat32(resp, cutStart, 1000)
	resp = AppendFloat32(resp, cutEnd, 1000)
	return resp
}

// buildGetStatsResponse returns usage statistics.
// Request: [cmd][mask:uint16], Response: [cmd][mask:uint32][fields...]
func (s *Simulator) buildGetStatsResponse(payload []byte) []byte {
	resp := []byte{byte(CommGetStats)}

	mask := uint32(0x07FF) // all 11 bits
	if len(payload) > 2 {
		mask = uint32(uint16(payload[1])<<8 | uint16(payload[2]))
	}
	resp = AppendUint32(resp, mask)

	samples := float64(s.state.StatSamples)
	if samples < 1 {
		samples = 1
	}
	// Bit 0: speed_avg (float32_auto)
	if mask&(1<<0) != 0 {
		resp = appendFloat32Auto(resp, s.state.SpeedSum/samples)
	}
	// Bit 1: speed_max (float32_auto)
	if mask&(1<<1) != 0 {
		resp = appendFloat32Auto(resp, s.state.SpeedMax)
	}
	// Bit 2: power_avg (float32_auto)
	if mask&(1<<2) != 0 {
		resp = appendFloat32Auto(resp, s.state.PowerSum/samples)
	}
	// Bit 3: power_max (float32_auto)
	if mask&(1<<3) != 0 {
		resp = appendFloat32Auto(resp, s.state.PowerMax)
	}
	// Bit 4: current_avg (float32_auto)
	if mask&(1<<4) != 0 {
		resp = appendFloat32Auto(resp, s.state.CurrentSum/samples)
	}
	// Bit 5: current_max (float32_auto)
	if mask&(1<<5) != 0 {
		resp = appendFloat32Auto(resp, s.state.CurrentMax)
	}
	// Bit 6: temp_mos_avg (float32_auto)
	if mask&(1<<6) != 0 {
		resp = appendFloat32Auto(resp, s.state.MOSFETTemp)
	}
	// Bit 7: temp_mos_max (float32_auto)
	if mask&(1<<7) != 0 {
		resp = appendFloat32Auto(resp, s.state.MOSFETTempMax)
	}
	// Bit 8: temp_motor_avg (float32_auto)
	if mask&(1<<8) != 0 {
		resp = appendFloat32Auto(resp, s.state.MotorTemp)
	}
	// Bit 9: temp_motor_max (float32_auto)
	if mask&(1<<9) != 0 {
		resp = appendFloat32Auto(resp, s.state.MotorTempMax)
	}
	// Bit 10: count_time (float32_auto) — uptime in seconds
	if mask&(1<<10) != 0 {
		resp = appendFloat32Auto(resp, float64(s.state.UptimeMs)/1000.0)
	}

	return resp
}

// buildQMLUIResponse returns empty QML UI data.
func (s *Simulator) buildQMLUIResponse(cmd CommPacketID, payload []byte) []byte {
	resp := []byte{byte(cmd)}
	resp = AppendInt32(resp, 0) // total size = 0 (no QML)
	resp = AppendInt32(resp, 0) // offset = 0
	return resp
}

// Motor detection responses.
// These simulate the detection process, returning values from the board profile
// or defaults. In real hardware, VESC spins the motor to measure these.

// buildDetectMotorRLResponse returns resistance and inductance.
// Response: [cmd][R*1e6 as int32][L*1e3 as int32][LdLq_diff*1e3 as int32]
func (s *Simulator) buildDetectMotorRLResponse() []byte {
	log.Printf("simulator: motor R/L detection (simulated)")
	resp := []byte{byte(CommDetectMotorRL)}
	resp = AppendFloat32(resp, s.state.MotorResistance, 1e6)
	resp = AppendFloat32(resp, s.state.MotorInductance, 1e3)
	resp = AppendFloat32(resp, 0, 1e3) // Ld-Lq difference
	return resp
}

// buildDetectMotorFluxResponse returns flux linkage.
// Response: [cmd][flux_linkage*1e7 as int32]
func (s *Simulator) buildDetectMotorFluxResponse() []byte {
	log.Printf("simulator: motor flux linkage detection (simulated)")
	resp := []byte{byte(CommDetectMotorFlux)}
	resp = AppendFloat32(resp, s.state.MotorFluxLinkage, 1e7)
	return resp
}

// buildDetectEncoderResponse returns encoder offset and ratio.
// Response: [cmd][offset*1e6 as int32][ratio*1e6 as int32][inverted as int8]
func (s *Simulator) buildDetectEncoderResponse() []byte {
	log.Printf("simulator: encoder detection (simulated - no encoder)")
	resp := []byte{byte(CommDetectEncoder)}
	resp = AppendFloat32(resp, 1001.0, 1e6) // 1001.0 = no encoder sentinel
	resp = AppendFloat32(resp, 0, 1e6)       // ratio = 0
	resp = append(resp, 0)                         // not inverted
	return resp
}

// buildDetectHallFOCResponse returns the hall sensor table.
// Response: [cmd][hall_table x8][result]
func (s *Simulator) buildDetectHallFOCResponse() []byte {
	log.Printf("simulator: Hall sensor FOC detection (simulated)")
	resp := []byte{byte(CommDetectHallFOC)}
	resp = append(resp, s.state.HallSensorTable[:]...)
	resp = append(resp, 0) // 0 = success
	return resp
}

// buildDetectMotorParamResponse returns legacy motor parameters.
// Response: [cmd][cycle_int_limit*1e3][bemf_coupling_k*1e3][hall_table x8][hall_res]
func (s *Simulator) buildDetectMotorParamResponse(payload []byte) []byte {
	log.Printf("simulator: legacy motor param detection (simulated)")
	resp := []byte{byte(CommDetectMotorParam)}
	resp = AppendFloat32(resp, 62.0, 1e3)  // cycle_int_limit
	resp = AppendFloat32(resp, 600.0, 1e3)  // bemf_coupling_k
	resp = append(resp, s.state.HallSensorTable[:]...)
	resp = append(resp, 0) // hall_res = 0 (success)
	return resp
}

// buildDetectMotorFluxOpenloopResponse returns flux linkage via openloop method.
// Response: [cmd][flux*1e7][enc_offset*1e6][enc_ratio*1e6][enc_inverted]
func (s *Simulator) buildDetectMotorFluxOpenloopResponse() []byte {
	log.Printf("simulator: openloop flux linkage detection (simulated)")
	resp := []byte{byte(CommDetectMotorFluxOpenloop)}
	resp = AppendFloat32(resp, s.state.MotorFluxLinkage, 1e7)
	resp = AppendFloat32(resp, 1001.0, 1e6) // no encoder
	resp = AppendFloat32(resp, 0, 1e6)       // ratio = 0
	resp = append(resp, 0)                         // not inverted
	return resp
}

// buildDetectApplyAllFOCResponse simulates the all-in-one FOC detection.
// Response: [cmd][result as int16] (0 = success)
func (s *Simulator) buildDetectApplyAllFOCResponse() []byte {
	log.Printf("simulator: apply all FOC detection (simulated)")
	resp := []byte{byte(CommDetectApplyAllFOC)}
	resp = AppendInt16(resp, 0) // success
	return resp
}
