package vesc

// CommPacketID represents VESC command identifiers from datatypes.h
type CommPacketID uint8

const (
	CommFWVersion         CommPacketID = 0
	CommJumpToBootloader  CommPacketID = 1
	CommEraseNewApp       CommPacketID = 2
	CommWriteNewAppData   CommPacketID = 3
	CommGetValues         CommPacketID = 4
	CommSetDuty           CommPacketID = 5
	CommSetCurrent        CommPacketID = 6
	CommSetCurrentBrake   CommPacketID = 7
	CommSetRPM            CommPacketID = 8
	CommSetPos            CommPacketID = 9
	CommSetHandbrake      CommPacketID = 10
	CommSetDetect         CommPacketID = 11
	CommSetServoPos       CommPacketID = 12
	CommSetMCConf         CommPacketID = 13
	CommGetMCConf         CommPacketID = 14
	CommGetMCConfDefault  CommPacketID = 15
	CommSetAppConf        CommPacketID = 16
	CommGetAppConf        CommPacketID = 17
	CommGetAppConfDefault CommPacketID = 18
	CommSamplePrint       CommPacketID = 19
	CommTerminalCmd       CommPacketID = 20
	CommPrintText         CommPacketID = 21
	CommRotor             CommPacketID = 22
	CommExperiment        CommPacketID = 23
	CommDetectMotorParam  CommPacketID = 24
	CommDetectMotorRL     CommPacketID = 25
	CommDetectMotorFlux   CommPacketID = 26
	CommDetectEncoder     CommPacketID = 27
	CommDetectHallFOC     CommPacketID = 28
	CommReboot            CommPacketID = 29
	CommAlive             CommPacketID = 30
	CommGetDecodedPPM     CommPacketID = 31
	CommGetDecodedADC     CommPacketID = 32
	CommGetDecodedChuk    CommPacketID = 33
	CommForwardCAN        CommPacketID = 34
	CommSetChuckData      CommPacketID = 35
	CommCustomAppData     CommPacketID = 36
	CommNRFStartPairing   CommPacketID = 37
	CommGetValuesSetup    CommPacketID = 50
	CommSetMCConfTemp     CommPacketID = 51
	CommGetMCConfTemp     CommPacketID = 52
	CommGetValuesSelective CommPacketID = 53
	CommPingCAN           CommPacketID = 57
)

// FaultCode from VESC datatypes.h
type FaultCode uint8

const (
	FaultNone                  FaultCode = 0
	FaultOverVoltage           FaultCode = 1
	FaultUnderVoltage          FaultCode = 2
	FaultDRV                   FaultCode = 3
	FaultAbsOverCurrent        FaultCode = 4
	FaultOverTempFET           FaultCode = 5
	FaultOverTempMotor         FaultCode = 6
	FaultGateDriverOverVoltage FaultCode = 7
	FaultGateDriverUnderVoltage FaultCode = 8
	FaultMCMOverCurrent        FaultCode = 9
	FaultBOOTingFromWatchdog   FaultCode = 10
	FaultEncoder               FaultCode = 11
)

func (f FaultCode) String() string {
	switch f {
	case FaultNone:
		return "NONE"
	case FaultOverVoltage:
		return "OVER_VOLTAGE"
	case FaultUnderVoltage:
		return "UNDER_VOLTAGE"
	case FaultDRV:
		return "DRV"
	case FaultAbsOverCurrent:
		return "ABS_OVER_CURRENT"
	case FaultOverTempFET:
		return "OVER_TEMP_FET"
	case FaultOverTempMotor:
		return "OVER_TEMP_MOTOR"
	default:
		return "UNKNOWN"
	}
}

// Values represents the data returned by COMM_GET_VALUES.
type Values struct {
	TempMOSFET    float64
	TempMotor     float64
	AvgMotorCurrent float64
	AvgInputCurrent float64
	DutyCycle     float64
	RPM           float64
	Voltage       float64
	AmpHours      float64
	AmpHoursCharged float64
	WattHours     float64
	WattHoursCharged float64
	Tachometer    int32
	TachometerAbs int32
	Fault         FaultCode
}

// FWVersion represents firmware version info.
type FWVersion struct {
	Major    uint8
	Minor    uint8
	HWName   string
	UUID     []byte
}
