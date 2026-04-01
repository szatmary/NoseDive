package main

import (
	"encoding/json"
	"fmt"
	"log"
)

var cmdNames = map[CommPacketID]string{
	CommFWVersion:              "FW_VERSION",
	CommGetValues:              "GET_VALUES",
	CommSetDuty:                "SET_DUTY",
	CommSetCurrent:             "SET_CURRENT",
	CommSetCurrentBrake:        "SET_CURRENT_BRAKE",
	CommSetRPM:                 "SET_RPM",
	CommSetPos:                 "SET_POS",
	CommSetHandbrake:           "SET_HANDBRAKE",
	CommSetMCConf:              "SET_MCCONF",
	CommGetMCConf:              "GET_MCCONF",
	CommGetMCConfDefault:       "GET_MCCONF_DEFAULT",
	CommSetAppConf:             "SET_APPCONF",
	CommGetAppConf:             "GET_APPCONF",
	CommGetAppConfDefault:      "GET_APPCONF_DEFAULT",
	CommTerminalCmd:            "TERMINAL_CMD",
	CommPrintText:              "PRINT_TEXT",
	CommDetectMotorParam:       "DETECT_MOTOR_PARAM",
	CommDetectMotorRL:          "DETECT_MOTOR_RL",
	CommDetectMotorFlux:        "DETECT_MOTOR_FLUX",
	CommDetectEncoder:          "DETECT_ENCODER",
	CommDetectHallFOC:          "DETECT_HALL_FOC",
	CommReboot:                 "REBOOT",
	CommAlive:                  "ALIVE",
	CommGetDecodedPPM:          "GET_DECODED_PPM",
	CommGetDecodedADC:          "GET_DECODED_ADC",
	CommGetDecodedChuk:         "GET_DECODED_CHUK",
	CommForwardCAN:             "FORWARD_CAN",
	CommSetChuckData:           "SET_CHUCK_DATA",
	CommCustomAppData:          "CUSTOM_APP_DATA",
	CommGetValuesSetup:         "GET_VALUES_SETUP",
	CommSetMCConfTemp:          "SET_MCCONF_TEMP",
	CommSetMCConfTempSetup:     "SET_MCCONF_TEMP_SETUP",
	CommGetValuesSelective:     "GET_VALUES_SELECTIVE",
	CommDetectMotorFluxOpenloop: "DETECT_MOTOR_FLUX_OPENLOOP",
	CommDetectApplyAllFOC:      "DETECT_APPLY_ALL_FOC",
	CommGetValuesSetupSelective: "GET_VALUES_SETUP_SELECTIVE",
	CommPingCAN:                "PING_CAN",
	CommAppDisableOutput:       "APP_DISABLE_OUTPUT",
	CommGetIMUData:             "GET_IMU_DATA",
	CommGetDecodedBalance:      "GET_DECODED_BALANCE",
	CommSetCurrentRel:          "SET_CURRENT_REL",
	CommSetBatteryCut:          "SET_BATTERY_CUT",
	CommSetBLEName:             "SET_BLE_NAME",
	CommSetBLEPin:              "SET_BLE_PIN",
	CommSetCANMode:             "SET_CAN_MODE",
	CommGetIMUCalibration:      "GET_IMU_CALIBRATION",
	CommGetMCConfTemp:          "GET_MCCONF_TEMP",
	CommGetCustomConfigXML:     "GET_CUSTOM_CONFIG_XML",
	CommGetCustomConfig:        "GET_CUSTOM_CONFIG",
	CommGetCustomConfigDefault: "GET_CUSTOM_CONFIG_DEFAULT",
	CommSetCustomConfig:        "SET_CUSTOM_CONFIG",
	CommBMSGetValues:           "BMS_GET_VALUES",
	CommSetOdometer:            "SET_ODOMETER",
	CommGetBatteryCut:          "GET_BATTERY_CUT",
	CommGetQMLUIHW:             "GET_QML_UI_HW",
	CommGetQMLUIApp:            "GET_QML_UI_APP",
	CommGetStats:               "GET_STATS",
	CommResetStats:             "RESET_STATS",
	CommQmluiErase:            "QMLUI_ERASE",
	CommQmluiWrite:            "QMLUI_WRITE",
	CommLispWriteCode:         "LISP_WRITE_CODE",
	CommLispEraseCode:         "LISP_ERASE_CODE",
	CommLispSetRunning:        "LISP_SET_RUNNING",
	CommLispGetStats:          "LISP_GET_STATS",
	CommLispReplCmd:           "LISP_REPL_CMD",
	CommLispStreamCode:        "LISP_STREAM_CODE",
	CommShutdown:               "SHUTDOWN",
	CommFWInfo:                 "FW_INFO",
}

func cmdName(cmd CommPacketID) string {
	if name, ok := cmdNames[cmd]; ok {
		return name
	}
	return fmt.Sprintf("UNKNOWN_0x%02x", cmd)
}

// logRx logs a received command with decoded details.
func logRx(payload []byte) {
	if len(payload) == 0 {
		return
	}
	cmd := CommPacketID(payload[0])
	name := cmdName(cmd)

	switch cmd {
	case CommSetMCConf:
		logRxMCConf(name, payload)
	case CommSetAppConf:
		log.Printf("← %s (%d bytes)", name, len(payload)-1)
	case CommDetectApplyAllFOC:
		logRxDetectAllFOC(name, payload)
	case CommCustomAppData:
		logRxCustomAppData(name, payload)
	case CommGetCustomConfigXML:
		if len(payload) >= 10 {
			idx := 2
			_ = GetInt32(payload, &idx) // requestLen
			offset := GetInt32(payload, &idx)
			log.Printf("← %s conf=%d offset=%d", name, payload[1], offset)
		} else {
			log.Printf("← %s len=%d", name, len(payload))
		}
	default:
		log.Printf("← %s len=%d", name, len(payload))
	}
}

// logTx logs a transmitted response with decoded details.
func logTx(resp []byte) {
	if len(resp) == 0 {
		return
	}
	cmd := CommPacketID(resp[0])
	name := cmdName(cmd)

	switch cmd {
	case CommGetMCConf, CommGetMCConfDefault:
		logTxMCConf(name, resp)
	case CommGetAppConf, CommGetAppConfDefault:
		log.Printf("→ %s (%d bytes)", name, len(resp)-1)
	case CommFWVersion:
		logTxFWVersion(name, resp)
	case CommGetValues:
		logTxValues(name, resp)
	case CommDetectApplyAllFOC:
		if len(resp) >= 3 {
			idx := 1
			result := GetInt16(resp, &idx)
			log.Printf("→ %s result=%d", name, result)
		}
	case CommDetectMotorRL:
		logTxDetectRL(name, resp)
	default:
		log.Printf("→ %s (%d bytes)", name, len(resp))
	}
}

func logRxMCConf(name string, payload []byte) {
	mcconf, ok := DeserializeMCConf(payload[1:])
	if !ok {
		log.Printf("← %s (%d bytes, could not decode)", name, len(payload)-1)
		return
	}
	logJSON("←", name, mcconf)
}

func logRxDetectAllFOC(name string, payload []byte) {
	if len(payload) < 22 {
		log.Printf("← %s len=%d", name, len(payload))
		return
	}
	idx := 1
	detectCAN := payload[idx] != 0; idx++
	maxPowerLoss := GetFloat32(payload, 1e3, &idx)
	minCurrentIn := GetFloat32(payload, 1e3, &idx)
	maxCurrentIn := GetFloat32(payload, 1e3, &idx)
	openloopRPM := GetFloat32(payload, 1e3, &idx)
	slERPM := GetFloat32(payload, 1e3, &idx)
	log.Printf("← %s can=%v max_power_loss=%.1fW current_in=[%.1f,%.1f]A openloop=%.0frpm sl=%.0ferpm",
		name, detectCAN, maxPowerLoss, minCurrentIn, maxCurrentIn, openloopRPM, slERPM)
}

func logRxCustomAppData(name string, payload []byte) {
	if len(payload) < 3 {
		log.Printf("← %s len=%d", name, len(payload))
		return
	}
	magic := payload[1]
	cmdID := CommandID(payload[2])
	var cmdStr string
	switch cmdID {
	case CommandInfo:
		cmdStr = "INFO"
	case CommandGetRTData:
		cmdStr = "GET_RT_DATA"
	case CommandGetAllData:
		cmdStr = "GET_ALL_DATA"
	case CommandRTTune:
		cmdStr = "RT_TUNE"
	case CommandCfgSave:
		cmdStr = "CFG_SAVE"
	case CommandHandtest:
		cmdStr = "HANDTEST"
	case CommandLCMPoll:
		cmdStr = "LCM_POLL"
	case CommandRealtimeData:
		cmdStr = "REALTIME_DATA"
	default:
		cmdStr = fmt.Sprintf("cmd_%d", cmdID)
	}
	if magic == PackageMagic {
		log.Printf("← %s refloat/%s", name, cmdStr)
	} else {
		log.Printf("← %s magic=0x%02x cmd=%d", name, magic, cmdID)
	}
}

func logTxFWVersion(name string, resp []byte) {
	if len(resp) < 4 {
		log.Printf("→ %s (%d bytes)", name, len(resp))
		return
	}
	major := resp[1]
	minor := resp[2]
	idx := 3
	hwName := GetString(resp, &idx)
	log.Printf("→ %s v%d.%02d hw=%q", name, major, minor, hwName)
}

func logTxValues(name string, resp []byte) {
	if len(resp) < 40 {
		log.Printf("→ %s (%d bytes)", name, len(resp))
		return
	}
	idx := 1
	mosfetTemp := GetFloat16(resp, 10, &idx)
	motorTemp := GetFloat16(resp, 10, &idx)
	motorCurrent := GetFloat32(resp, 100, &idx)
	inputCurrent := GetFloat32(resp, 100, &idx)
	_ = GetFloat32(resp, 100, &idx) // avg_id
	_ = GetFloat32(resp, 100, &idx) // avg_iq
	duty := GetFloat16(resp, 1000, &idx)
	rpm := GetFloat32(resp, 1, &idx)
	voltage := GetFloat16(resp, 10, &idx)
	log.Printf("→ %s V=%.1f duty=%.1f%% rpm=%.0f I_motor=%.1fA I_in=%.1fA T_mos=%.0f°C T_mot=%.0f°C",
		name, voltage, duty*100, rpm, motorCurrent, inputCurrent, mosfetTemp, motorTemp)
}

func logJSON(direction, name string, v any) {
	j, err := json.MarshalIndent(v, "  ", "  ")
	if err != nil {
		log.Printf("%s %s (json error: %v)", direction, name, err)
		return
	}
	log.Printf("%s %s:\n  %s", direction, name, j)
}

func logTxMCConf(name string, resp []byte) {
	mcconf, ok := DeserializeMCConf(resp[1:])
	if !ok {
		log.Printf("→ %s (%d bytes, could not decode)", name, len(resp)-1)
		return
	}
	logJSON("→", name, mcconf)
}

func logTxDetectRL(name string, resp []byte) {
	if len(resp) < 13 {
		log.Printf("→ %s (%d bytes)", name, len(resp))
		return
	}
	idx := 1
	r := GetFloat32(resp, 1e6, &idx)
	l := GetFloat32(resp, 1e3, &idx)
	ldlq := GetFloat32(resp, 1e3, &idx)
	log.Printf("→ %s R=%.4fΩ L=%.1fµH Ld-Lq=%.1fµH", name, r, l*1e6, ldlq*1e6)
}
