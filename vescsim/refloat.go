package main

import (
	"fmt"

)

// parseAllData decodes COMMAND_GET_ALLDATA (10) response.
// Layout uses a mix of float16(scale), uint8 with offset, and int16.
func parseAllData(data []byte, mode uint8) (*RTData, error) {
	if len(data) < 32 {
		return nil, fmt.Errorf("alldata response too short: %d bytes", len(data))
	}

	rt := &RTData{}
	idx := 0

	// Byte 0: mode echo (or 69 if VESC fault)
	respMode := data[idx]
	idx++
	if respMode == 69 {
		return nil, fmt.Errorf("VESC fault detected")
	}

	// Bytes 1-2: balance_current (float16, scale=10)
	rt.BalanceCurrent = float64(GetInt16(data, &idx)) / 10.0
	// Bytes 3-4: balance_pitch (float16, scale=10)
	rt.BalancePitch = float64(GetInt16(data, &idx)) / 10.0
	// Bytes 5-6: roll (float16, scale=10)
	rt.Roll = float64(GetInt16(data, &idx)) / 10.0

	// Byte 7: (state_compat & 0xF) | (sat_compat << 4)
	stateByte := data[idx]
	idx++
	stateCompat := stateByte & 0x0F
	satCompat := stateByte >> 4
	rt.State.RunState = decodeStateCompat(stateCompat)
	rt.State.StopCondition = decodeStopCompat(stateCompat)
	rt.State.SAT = decodeSATCompat(satCompat)

	// Byte 8: (switch_state & 0xF) | (beep_reason << 4)
	switchByte := data[idx]
	idx++
	rt.State.Footpad = FootpadState(switchByte & 0x0F)

	// Byte 9: adc1 * 50
	rt.ADC1 = float64(data[idx]) / 50.0
	idx++
	// Byte 10: adc2 * 50
	rt.ADC2 = float64(data[idx]) / 50.0
	idx++

	// Bytes 11-16: setpoints as uint8 * 5 + 128
	rt.Setpoint = (float64(data[idx]) - 128.0) / 5.0
	idx++
	rt.ATRSetpoint = (float64(data[idx]) - 128.0) / 5.0
	idx++
	rt.BrakeTiltSetpoint = (float64(data[idx]) - 128.0) / 5.0
	idx++
	rt.TorqueTiltSetpoint = (float64(data[idx]) - 128.0) / 5.0
	idx++
	rt.TurnTiltSetpoint = (float64(data[idx]) - 128.0) / 5.0
	idx++
	rt.RemoteSetpoint = (float64(data[idx]) - 128.0) / 5.0
	idx++

	// Bytes 17-18: pitch (float16, scale=10)
	rt.Pitch = float64(GetInt16(data, &idx)) / 10.0

	// Byte 19: booster current + 128
	rt.BoosterCurrent = float64(data[idx]) - 128.0
	idx++

	// Bytes 20-21: battery voltage (float16, scale=10)
	rt.BattVoltage = float64(GetInt16(data, &idx)) / 10.0

	// Bytes 22-23: erpm (int16)
	rt.ERPM = float64(GetInt16(data, &idx))

	// Bytes 24-25: speed m/s (float16, scale=10)
	rt.Speed = float64(GetInt16(data, &idx)) / 10.0

	// Bytes 26-27: motor current (float16, scale=10)
	rt.MotorCurrent = float64(GetInt16(data, &idx)) / 10.0

	// Bytes 28-29: battery current (float16, scale=10)
	rt.BattCurrent = float64(GetInt16(data, &idx)) / 10.0

	// Byte 30: duty * 100 + 128
	rt.DutyCycle = (float64(data[idx]) - 128.0) / 100.0
	idx++

	// Byte 31: foc_id
	idx++ // skip

	// Mode >= 2: additional data
	if mode >= 2 && idx+8 <= len(data) {
		// float32_auto: distance (skip for now)
		idx += 4
		// mosfet_temp * 2
		rt.MOSFETTemp = float64(data[idx]) / 2.0
		idx++
		// motor_temp * 2
		rt.MotorTemp = float64(data[idx]) / 2.0
		idx++
		// reserved batt_temp
		idx++
	}

	return rt, nil
}

// parseRTData decodes COMMAND_GET_RTDATA (1) response.
// Uses float32_auto encoding for all values.
func parseRTData(data []byte) (*RTData, error) {
	if len(data) < 40 {
		return nil, fmt.Errorf("rtdata response too short: %d bytes", len(data))
	}

	rt := &RTData{}
	idx := 0

	rt.BalanceCurrent = GetFloat32Auto(data, &idx)
	rt.BalancePitch = GetFloat32Auto(data, &idx)
	rt.Roll = GetFloat32Auto(data, &idx)

	// State byte: (state_compat & 0xF) | (sat_compat << 4)
	stateByte := data[idx]
	idx++
	stateCompat := stateByte & 0x0F
	satCompat := stateByte >> 4
	rt.State.RunState = decodeStateCompat(stateCompat)
	rt.State.StopCondition = decodeStopCompat(stateCompat)
	rt.State.SAT = decodeSATCompat(satCompat)

	// Switch byte: (switch_state & 0xF) | (beep_reason << 4)
	switchByte := data[idx]
	idx++
	rt.State.Footpad = FootpadState(switchByte & 0x0F)

	rt.ADC1 = GetFloat32Auto(data, &idx)
	rt.ADC2 = GetFloat32Auto(data, &idx)
	rt.Setpoint = GetFloat32Auto(data, &idx)
	rt.ATRSetpoint = GetFloat32Auto(data, &idx)
	rt.BrakeTiltSetpoint = GetFloat32Auto(data, &idx)
	rt.TorqueTiltSetpoint = GetFloat32Auto(data, &idx)
	rt.TurnTiltSetpoint = GetFloat32Auto(data, &idx)
	rt.RemoteSetpoint = GetFloat32Auto(data, &idx)
	rt.Pitch = GetFloat32Auto(data, &idx)
	rt.FiltCurrent = GetFloat32Auto(data, &idx)

	if idx+4 <= len(data) {
		rt.ATRAccelDiff = GetFloat32Auto(data, &idx)
	}
	if idx+4 <= len(data) {
		rt.BoosterCurrent = GetFloat32Auto(data, &idx)
	}
	if idx+4 <= len(data) {
		rt.DirCurrent = GetFloat32Auto(data, &idx)
	}
	if idx+4 <= len(data) {
		rt.RemoteInput = GetFloat32Auto(data, &idx)
	}

	return rt, nil
}

// Legacy state_compat decoding
func decodeStateCompat(v uint8) RunState {
	switch v {
	case 0: // STARTUP
		return StateStartup
	case 1, 2, 3, 4, 5: // RUNNING variants
		return StateRunning
	case 6, 7, 8, 9, 11, 12, 13: // FAULT variants
		return StateReady
	case 14: // CHARGING
		return StateReady
	case 15: // DISABLED
		return StateDisabled
	default:
		return StateReady
	}
}

func decodeStopCompat(v uint8) StopCondition {
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

func decodeSATCompat(v uint8) SetpointAdjustmentType {
	switch v {
	case 0:
		return SATCentering
	case 1:
		return SATReverseStop
	case 2:
		return SATNone
	case 3:
		return SATPBDuty
	case 4:
		return SATPBHighVoltage
	case 5:
		return SATPBLowVoltage
	case 6:
		return SATPBTemperature
	case 7:
		return SATPBSpeed
	case 8:
		return SATPBError
	default:
		return SATNone
	}
}

// handleCustomAppData is in simulator.go (with all the response builders)
