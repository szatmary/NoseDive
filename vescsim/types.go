package main

const PackageMagic byte = 101 // 0x65 - Refloat package ID

// Command IDs from Refloat main.c
type CommandID uint8

const (
	CommandInfo             CommandID = 0
	CommandGetRTData        CommandID = 1
	CommandRTTune           CommandID = 2
	CommandTuneDefaults     CommandID = 3
	CommandCfgSave          CommandID = 4
	CommandCfgRestore       CommandID = 5
	CommandTuneOther        CommandID = 6
	CommandRCMove           CommandID = 7
	CommandBooster          CommandID = 8
	CommandPrintInfo        CommandID = 9
	CommandGetAllData       CommandID = 10
	CommandExperiment       CommandID = 11
	CommandLock             CommandID = 12
	CommandHandtest         CommandID = 13
	CommandTuneTilt         CommandID = 14
	CommandLightsControl    CommandID = 20
	CommandFlywheel         CommandID = 22
	CommandLCMPoll          CommandID = 24
	CommandLCMLightInfo     CommandID = 25
	CommandLCMLightCtrl     CommandID = 26
	CommandLCMDeviceInfo    CommandID = 27
	CommandChargingState    CommandID = 28
	CommandLCMGetBattery    CommandID = 29
	CommandRealtimeData     CommandID = 31
	CommandRealtimeDataIDs  CommandID = 32
	CommandAlertsList       CommandID = 35
	CommandAlertsControl    CommandID = 36
	CommandDataRecordReq    CommandID = 41
)

// RunState from state.h
type RunState uint8

const (
	StateDisabled RunState = 0
	StateStartup  RunState = 1
	StateReady    RunState = 2
	StateRunning  RunState = 3
)

func (s RunState) String() string {
	switch s {
	case StateDisabled:
		return "DISABLED"
	case StateStartup:
		return "STARTUP"
	case StateReady:
		return "READY"
	case StateRunning:
		return "RUNNING"
	default:
		return "UNKNOWN"
	}
}

// Mode from state.h
type Mode uint8

const (
	ModeNormal   Mode = 0
	ModeHandtest Mode = 1
	ModeFlywheel Mode = 2
)

func (m Mode) String() string {
	switch m {
	case ModeNormal:
		return "NORMAL"
	case ModeHandtest:
		return "HANDTEST"
	case ModeFlywheel:
		return "FLYWHEEL"
	default:
		return "UNKNOWN"
	}
}

// StopCondition from state.h
type StopCondition uint8

const (
	StopNone        StopCondition = 0
	StopPitch       StopCondition = 1
	StopRoll        StopCondition = 2
	StopSwitchHalf  StopCondition = 3
	StopSwitchFull  StopCondition = 4
	StopReverseStop StopCondition = 5
	StopQuickstop   StopCondition = 6
)

func (s StopCondition) String() string {
	switch s {
	case StopNone:
		return "NONE"
	case StopPitch:
		return "PITCH"
	case StopRoll:
		return "ROLL"
	case StopSwitchHalf:
		return "SWITCH_HALF"
	case StopSwitchFull:
		return "SWITCH_FULL"
	case StopReverseStop:
		return "REVERSE_STOP"
	case StopQuickstop:
		return "QUICKSTOP"
	default:
		return "UNKNOWN"
	}
}

// SetpointAdjustmentType from state.h
type SetpointAdjustmentType uint8

const (
	SATNone          SetpointAdjustmentType = 0
	SATCentering     SetpointAdjustmentType = 1
	SATReverseStop   SetpointAdjustmentType = 2
	SATPBSpeed       SetpointAdjustmentType = 5
	SATPBDuty        SetpointAdjustmentType = 6
	SATPBError       SetpointAdjustmentType = 7
	SATPBHighVoltage SetpointAdjustmentType = 10
	SATPBLowVoltage  SetpointAdjustmentType = 11
	SATPBTemperature SetpointAdjustmentType = 12
)

func (s SetpointAdjustmentType) String() string {
	switch s {
	case SATNone:
		return "NONE"
	case SATCentering:
		return "CENTERING"
	case SATReverseStop:
		return "REVERSESTOP"
	case SATPBSpeed:
		return "PUSHBACK_SPEED"
	case SATPBDuty:
		return "PUSHBACK_DUTY"
	case SATPBError:
		return "PUSHBACK_ERROR"
	case SATPBHighVoltage:
		return "PUSHBACK_HV"
	case SATPBLowVoltage:
		return "PUSHBACK_LV"
	case SATPBTemperature:
		return "PUSHBACK_TEMP"
	default:
		return "UNKNOWN"
	}
}

// FootpadState from footpad_sensor.h
type FootpadState uint8

const (
	FootpadNone  FootpadState = 0
	FootpadLeft  FootpadState = 1
	FootpadRight FootpadState = 2
	FootpadBoth  FootpadState = 3
)

func (f FootpadState) String() string {
	switch f {
	case FootpadNone:
		return "NONE"
	case FootpadLeft:
		return "LEFT"
	case FootpadRight:
		return "RIGHT"
	case FootpadBoth:
		return "BOTH"
	default:
		return "UNKNOWN"
	}
}

// State combines all state fields.
type State struct {
	RunState      RunState
	Mode          Mode
	SAT           SetpointAdjustmentType
	StopCondition StopCondition
	Footpad       FootpadState
	Charging      bool
	Wheelslip     bool
	DarkRide      bool
}

// RTData represents real-time telemetry from Refloat.
type RTData struct {
	State State

	// Motor data
	Speed        float64 // m/s
	ERPM         float64
	MotorCurrent float64
	DirCurrent   float64
	FiltCurrent  float64
	DutyCycle    float64
	BattVoltage  float64
	BattCurrent  float64
	MOSFETTemp   float64
	MotorTemp    float64

	// IMU data
	Pitch        float64
	BalancePitch float64
	Roll         float64

	// Footpad ADC
	ADC1 float64
	ADC2 float64

	// Remote
	RemoteInput float64

	// Runtime setpoints (only valid when running)
	Setpoint           float64
	ATRSetpoint        float64
	BrakeTiltSetpoint  float64
	TorqueTiltSetpoint float64
	TurnTiltSetpoint   float64
	RemoteSetpoint     float64
	BalanceCurrent     float64
	ATRAccelDiff       float64
	ATRSpeedBoost      float64
	BoosterCurrent     float64
}

// PackageInfo holds Refloat package version info from COMMAND_INFO.
type PackageInfo struct {
	Name         string
	MajorVersion uint8
	MinorVersion uint8
	PatchVersion uint8
	Suffix       string
}
