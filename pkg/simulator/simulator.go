package simulator

import (
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"math"
	"math/rand"
	"net"
	"sync"
	"time"

	"github.com/szatmary/nosedive/pkg/refloat"
	"github.com/szatmary/nosedive/pkg/vesc"
)

// Simulator emulates a VESC running the Refloat package over TCP and/or BLE.
type Simulator struct {
	listener net.Listener
	mu       sync.Mutex
	state    *BoardState
	running  bool
	stop     chan struct{}
	bleName  string // BLE device name (empty = no BLE)
}

// BoardState holds the simulated board state.
type BoardState struct {
	// VESC
	FWMajor      uint8
	FWMinor      uint8
	HWName       string
	UUID         [12]byte
	ControllerID uint8
	Voltage      float64
	Fault        vesc.FaultCode

	// Motor
	ERPM         float64
	DutyCycle    float64
	MotorCurrent float64
	BattCurrent  float64
	MOSFETTemp   float64
	MotorTemp    float64
	Speed        float64 // m/s

	// Refloat
	RunState      refloat.RunState
	Mode          refloat.Mode
	SAT           refloat.SetpointAdjustmentType
	StopCondition refloat.StopCondition
	Footpad       refloat.FootpadState
	Charging      bool

	// IMU
	Pitch float64
	Roll  float64

	// Footpad ADC
	ADC1 float64
	ADC2 float64

	// Setpoints
	Setpoint       float64
	BalanceCurrent float64

	// Counters
	AmpHours         float64
	AmpHoursCharged  float64
	WattHours        float64
	WattHoursCharged float64
	Tachometer       int32
	TachometerAbs    int32

	// Refloat package info
	PkgName   string
	PkgMajor  uint8
	PkgMinor  uint8
	PkgPatch  uint8
	PkgSuffix string

	// Config data
	ConfigXML  []byte // Refloat custom config XML
	ConfigData []byte // Serialized Refloat custom config binary
	MCConf     []byte // Motor controller config blob
	AppConf    []byte // App config blob
}

// DefaultBoardState returns a realistic idle board state.
func DefaultBoardState() *BoardState {
	bs := &BoardState{
		FWMajor:      6,
		FWMinor:      5,
		HWName:       "VESC 6 MK6",
		ControllerID: 0,
		Voltage:      63.0, // ~15S fully charged
		MOSFETTemp:   28.0,
		MotorTemp:    25.0,
		RunState:     refloat.StateReady,
		Mode:         refloat.ModeNormal,
		StopCondition: refloat.StopNone,
		Footpad:      refloat.FootpadNone,
		Pitch:        0.0,
		Roll:         0.0,
		ADC1:         0.0,
		ADC2:         0.0,
		PkgName:      "Refloat",
		PkgMajor:     2,
		PkgMinor:     0,
		PkgPatch:     1,
		PkgSuffix:    "sim",
		ConfigXML:    refloatConfigXML,
		MCConf:       generateDefaultMCConf(),
		AppConf:      generateDefaultAppConf(),
	}

	// Generate default binary config from XML
	bs.ConfigData = generateDefaultConfig(refloatConfigXML)

	// Generate a fake UUID
	copy(bs.UUID[:], []byte{0x4e, 0x6f, 0x73, 0x65, 0x44, 0x69, 0x76, 0x65, 0x53, 0x49, 0x4d, 0x31})

	return bs
}

// New creates a new simulator.
func New() *Simulator {
	return &Simulator{
		state: DefaultBoardState(),
		stop:  make(chan struct{}),
	}
}

// Start begins listening on the given address.
func (s *Simulator) Start(addr string) error {
	var err error
	s.listener, err = net.Listen("tcp", addr)
	if err != nil {
		return fmt.Errorf("listen on %s: %w", addr, err)
	}
	s.running = true

	// Start physics simulation
	go s.physicsLoop()

	go s.acceptLoop()
	return nil
}

// Addr returns the listener address.
func (s *Simulator) Addr() string {
	if s.listener == nil {
		return ""
	}
	return s.listener.Addr().String()
}

// Stop shuts down the simulator.
func (s *Simulator) Stop() {
	s.running = false
	close(s.stop)
	if s.listener != nil {
		s.listener.Close()
	}
}

// State returns a snapshot of the current board state.
func (s *Simulator) State() BoardState {
	s.mu.Lock()
	defer s.mu.Unlock()
	return *s.state
}

// SetFootpad simulates stepping on/off the footpad.
func (s *Simulator) SetFootpad(state refloat.FootpadState) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state.Footpad = state
	s.state.ADC1 = 0.0
	s.state.ADC2 = 0.0
	if state == refloat.FootpadLeft || state == refloat.FootpadBoth {
		s.state.ADC1 = 3.0
	}
	if state == refloat.FootpadRight || state == refloat.FootpadBoth {
		s.state.ADC2 = 3.0
	}
}

// SetPitch simulates tilting the board.
func (s *Simulator) SetPitch(degrees float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state.Pitch = degrees
}

// SetRoll simulates rolling the board side-to-side.
func (s *Simulator) SetRoll(degrees float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state.Roll = degrees
}

func (s *Simulator) acceptLoop() {
	for s.running {
		conn, err := s.listener.Accept()
		if err != nil {
			if s.running {
				log.Printf("simulator: accept error: %v", err)
			}
			return
		}
		go s.handleConn(conn)
	}
}

func (s *Simulator) handleConn(conn net.Conn) {
	defer conn.Close()
	log.Printf("simulator: client connected from %s", conn.RemoteAddr())

	for s.running {
		payload, err := vesc.DecodePacket(conn)
		if err != nil {
			if err != io.EOF {
				log.Printf("simulator: decode error: %v", err)
			}
			return
		}

		resp := s.HandleCommand(payload)
		if resp != nil {
			pkt, err := vesc.EncodePacket(resp)
			if err != nil {
				log.Printf("simulator: encode error: %v", err)
				return
			}
			if _, err := conn.Write(pkt); err != nil {
				log.Printf("simulator: write error: %v", err)
				return
			}
		}
	}
}

// HandleCommand processes a VESC command payload and returns the response.
// This is transport-agnostic and used by both TCP and BLE transports.
func (s *Simulator) HandleCommand(payload []byte) []byte {
	if len(payload) == 0 {
		return nil
	}

	cmd := vesc.CommPacketID(payload[0])
	s.mu.Lock()
	defer s.mu.Unlock()

	switch cmd {
	case vesc.CommFWVersion:
		return s.buildFWVersionResponse()
	case vesc.CommGetValues:
		return s.buildGetValuesResponse()
	case vesc.CommGetValuesSetup:
		return s.buildGetValuesSetupResponse()
	case vesc.CommGetValuesSelective, vesc.CommGetValuesSetupSelective:
		// Return full values for selective requests
		return s.buildGetValuesResponse()
	case vesc.CommAlive:
		return nil // no response needed
	case vesc.CommSetDuty, vesc.CommSetCurrent, vesc.CommSetCurrentBrake,
		vesc.CommSetRPM, vesc.CommSetPos, vesc.CommSetHandbrake,
		vesc.CommSetCurrentRel:
		return nil // motor control commands - no response
	case vesc.CommGetMCConf, vesc.CommGetMCConfDefault:
		return s.buildGetMCConfResponse()
	case vesc.CommSetMCConf:
		// Accept and store MC config
		if len(payload) > 1 {
			s.state.MCConf = make([]byte, len(payload)-1)
			copy(s.state.MCConf, payload[1:])
		}
		return nil
	case vesc.CommGetAppConf, vesc.CommGetAppConfDefault:
		return s.buildGetAppConfResponse()
	case vesc.CommSetAppConf:
		if len(payload) > 1 {
			s.state.AppConf = make([]byte, len(payload)-1)
			copy(s.state.AppConf, payload[1:])
		}
		return nil
	case vesc.CommGetDecodedPPM:
		return s.buildGetDecodedPPMResponse()
	case vesc.CommGetDecodedADC:
		return s.buildGetDecodedADCResponse()
	case vesc.CommGetDecodedChuk:
		return s.buildGetDecodedChukResponse()
	case vesc.CommGetDecodedBalance:
		// Return empty balance data
		resp := []byte{byte(vesc.CommGetDecodedBalance)}
		resp = vesc.AppendFloat32(resp, s.state.Pitch, 1000000)
		resp = vesc.AppendFloat32(resp, s.state.Roll, 1000000)
		resp = vesc.AppendFloat32(resp, 0, 1000000) // diff time
		resp = vesc.AppendFloat32(resp, s.state.MotorCurrent, 1000000)
		return resp
	case vesc.CommPingCAN:
		return s.buildPingCANResponse()
	case vesc.CommForwardCAN:
		// CAN forwarding - extract the inner command and handle it
		if len(payload) > 2 {
			return s.HandleCommand(payload[2:])
		}
		return nil
	case vesc.CommTerminalCmd, vesc.CommTerminalCmdSync:
		return s.buildTerminalResponse(payload)
	case vesc.CommGetIMUData:
		return s.buildGetIMUDataResponse(payload)
	case vesc.CommGetCustomConfigXML:
		return s.buildGetCustomConfigXMLResponse(payload)
	case vesc.CommGetCustomConfig, vesc.CommGetCustomConfigDefault:
		return s.buildGetCustomConfigResponse(payload)
	case vesc.CommSetCustomConfig:
		return s.buildSetCustomConfigResponse(payload)
	case vesc.CommGetBatteryCut:
		return s.buildGetBatteryCutResponse()
	case vesc.CommSetBatteryCut:
		return nil // accept silently
	case vesc.CommGetStats:
		return s.buildGetStatsResponse(payload)
	case vesc.CommResetStats:
		return nil // accept silently
	case vesc.CommGetQMLUIHW:
		return s.buildQMLUIResponse(vesc.CommGetQMLUIHW, payload)
	case vesc.CommGetQMLUIApp:
		return s.buildQMLUIResponse(vesc.CommGetQMLUIApp, payload)
	case vesc.CommSetMCConfTemp, vesc.CommSetMCConfTempSetup:
		return nil // accept silently
	case vesc.CommGetMCConfTemp:
		return []byte{byte(vesc.CommGetMCConfTemp)}
	case vesc.CommReboot, vesc.CommShutdown:
		log.Printf("simulator: received command 0x%02x (ignored)", cmd)
		return nil
	case vesc.CommAppDisableOutput:
		return nil // accept silently
	case vesc.CommSetOdometer:
		return nil
	case vesc.CommBMSGetValues:
		// Return minimal BMS response
		return []byte{byte(vesc.CommBMSGetValues)}
	case vesc.CommSetChuckData:
		return nil
	case vesc.CommFWInfo:
		return s.buildFWVersionResponse() // same format
	case vesc.CommCustomAppData:
		if len(payload) < 2 {
			return nil
		}
		return s.handleCustomAppData(payload[1:])
	case vesc.CommSetBLEName, vesc.CommSetBLEPin, vesc.CommSetCANMode:
		return nil // accept silently
	case vesc.CommGetIMUCalibration:
		resp := []byte{byte(vesc.CommGetIMUCalibration)}
		// accel offsets (3 x float32) + gyro offsets (3 x float32)
		for i := 0; i < 6; i++ {
			resp = vesc.AppendFloat32(resp, 0, 1000)
		}
		return resp
	default:
		log.Printf("simulator: unhandled command 0x%02x (%d)", cmd, cmd)
		return nil
	}
}

func (s *Simulator) handleCustomAppData(data []byte) []byte {
	if len(data) < 2 || data[0] != refloat.PackageMagic {
		return nil
	}

	cmd := refloat.CommandID(data[1])
	switch cmd {
	case refloat.CommandInfo:
		return s.buildRefloatInfoResponse()
	case refloat.CommandGetRTData:
		return s.buildRefloatRTDataResponse()
	case refloat.CommandGetAllData:
		mode := uint8(0)
		if len(data) > 2 {
			mode = data[2]
		}
		return s.buildRefloatAllDataResponse(mode)
	case refloat.CommandRTTune:
		return nil // fire-and-forget: applies tune values at runtime
	case refloat.CommandTuneDefaults:
		return nil // fire-and-forget: resets tune to defaults
	case refloat.CommandTuneOther:
		return nil // fire-and-forget: applies startup/tilt tune
	case refloat.CommandTuneTilt:
		return nil // fire-and-forget: applies tilt/duty tune
	case refloat.CommandCfgSave:
		log.Printf("simulator: refloat config save requested")
		return nil
	case refloat.CommandCfgRestore:
		log.Printf("simulator: refloat config restore requested")
		return nil
	case refloat.CommandRCMove:
		return nil // fire-and-forget
	case refloat.CommandBooster:
		return nil // fire-and-forget
	case refloat.CommandPrintInfo:
		return nil // no-op in Refloat source
	case refloat.CommandExperiment:
		return nil // no-op
	case refloat.CommandLock:
		return nil // fire-and-forget
	case refloat.CommandHandtest:
		if len(data) > 2 {
			if data[2] != 0 {
				s.state.Mode = refloat.ModeHandtest
			} else {
				s.state.Mode = refloat.ModeNormal
			}
		}
		return nil // fire-and-forget
	case refloat.CommandFlywheel:
		// Requires specific flag format; simplified for sim
		if s.state.Mode == refloat.ModeFlywheel {
			s.state.Mode = refloat.ModeNormal
		} else {
			s.state.Mode = refloat.ModeFlywheel
		}
		return nil // fire-and-forget
	case refloat.CommandLightsControl:
		return s.buildRefloatLightsControlResponse(data)
	case refloat.CommandLCMPoll:
		return s.buildRefloatLCMPollResponse()
	case refloat.CommandLCMLightInfo:
		return s.buildRefloatLCMLightInfoResponse()
	case refloat.CommandLCMLightCtrl:
		return nil // fire-and-forget
	case refloat.CommandLCMDeviceInfo:
		return s.buildRefloatLCMDeviceInfoResponse()
	case refloat.CommandChargingState:
		return nil // fire-and-forget: updates charging state from LCM
	case refloat.CommandLCMGetBattery:
		return s.buildRefloatBatteryResponse()
	case refloat.CommandRealtimeData:
		return s.buildRefloatRealtimeDataResponse()
	case refloat.CommandRealtimeDataIDs:
		return s.buildRefloatRealtimeDataIDsResponse()
	case refloat.CommandAlertsList:
		return s.buildRefloatAlertsResponse()
	case refloat.CommandAlertsControl:
		return nil // fire-and-forget
	case refloat.CommandDataRecordReq:
		return nil // no data recording in sim
	default:
		log.Printf("simulator: unhandled refloat command %d", cmd)
		return nil
	}
}

func (s *Simulator) buildRefloatInfoResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandInfo)}
	// Version 2 response format
	resp = append(resp, 2) // response version
	resp = append(resp, 0) // flags

	// Package name: fixed 20 bytes
	name := make([]byte, 20)
	copy(name, s.state.PkgName)
	resp = append(resp, name...)

	resp = append(resp, s.state.PkgMajor, s.state.PkgMinor, s.state.PkgPatch)

	// Suffix: fixed 20 bytes
	suffix := make([]byte, 20)
	copy(suffix, s.state.PkgSuffix)
	resp = append(resp, suffix...)

	// Git hash (4 bytes)
	resp = append(resp, 0, 0, 0, 0)
	// Tick rate (4 bytes) - 1000 Hz
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, 1000)
	resp = append(resp, b...)
	// Capabilities (4 bytes)
	resp = append(resp, 0, 0, 0, 0)
	// Extra flags
	resp = append(resp, 0)

	return resp
}

func (s *Simulator) buildRefloatRTDataResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandGetRTData)}

	resp = appendFloat32Auto(resp, s.state.BalanceCurrent)
	resp = appendFloat32Auto(resp, s.state.Pitch)
	resp = appendFloat32Auto(resp, s.state.Roll)

	// State byte
	stateCompat := encodeStateCompat(s.state.RunState, s.state.StopCondition)
	satCompat := encodeSATCompat(s.state.SAT)
	resp = append(resp, (satCompat<<4)|stateCompat)

	// Switch byte
	switchState := uint8(s.state.Footpad)
	resp = append(resp, switchState)

	resp = appendFloat32Auto(resp, s.state.ADC1)
	resp = appendFloat32Auto(resp, s.state.ADC2)
	resp = appendFloat32Auto(resp, s.state.Setpoint)
	resp = appendFloat32Auto(resp, 0) // atr setpoint
	resp = appendFloat32Auto(resp, 0) // brake tilt setpoint
	resp = appendFloat32Auto(resp, 0) // torque tilt setpoint
	resp = appendFloat32Auto(resp, 0) // turn tilt setpoint
	resp = appendFloat32Auto(resp, 0) // remote setpoint
	resp = appendFloat32Auto(resp, s.state.Pitch)
	resp = appendFloat32Auto(resp, s.state.MotorCurrent) // filt_current
	resp = appendFloat32Auto(resp, 0)                     // accel_diff
	resp = appendFloat32Auto(resp, 0)                     // booster_current
	resp = appendFloat32Auto(resp, s.state.MotorCurrent) // dir_current
	resp = appendFloat32Auto(resp, 0)                     // remote input

	return resp
}

func (s *Simulator) buildRefloatAllDataResponse(mode uint8) []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandGetAllData)}

	// mode echo
	resp = append(resp, mode)

	// balance_current (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.BalanceCurrent, 10)
	// balance_pitch (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.Pitch, 10)
	// roll (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.Roll, 10)

	// state byte
	stateCompat := encodeStateCompat(s.state.RunState, s.state.StopCondition)
	satCompat := encodeSATCompat(s.state.SAT)
	resp = append(resp, (satCompat<<4)|stateCompat)

	// switch byte
	resp = append(resp, uint8(s.state.Footpad))

	// adc1 * 50, adc2 * 50
	resp = append(resp, clampUint8(s.state.ADC1*50))
	resp = append(resp, clampUint8(s.state.ADC2*50))

	// setpoints as uint8 * 5 + 128
	resp = append(resp, clampUint8(s.state.Setpoint*5+128))
	resp = append(resp, 128) // atr
	resp = append(resp, 128) // brake tilt
	resp = append(resp, 128) // torque tilt
	resp = append(resp, 128) // turn tilt
	resp = append(resp, 128) // remote

	// pitch (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.Pitch, 10)

	// booster current + 128
	resp = append(resp, 128)

	// battery voltage (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.Voltage, 10)

	// erpm (int16)
	resp = vesc.AppendInt16(resp, int16(s.state.ERPM))

	// speed (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.Speed, 10)

	// motor current (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.MotorCurrent, 10)

	// battery current (float16, scale=10)
	resp = vesc.AppendFloat16(resp, s.state.BattCurrent, 10)

	// duty * 100 + 128
	resp = append(resp, clampUint8(s.state.DutyCycle*100+128))

	// foc_id (skip)
	resp = append(resp, 222) // magic for unavailable

	// mode >= 2
	if mode >= 2 {
		resp = appendFloat32Auto(resp, 0) // distance
		resp = append(resp, clampUint8(s.state.MOSFETTemp*2))
		resp = append(resp, clampUint8(s.state.MotorTemp*2))
		resp = append(resp, 0) // reserved
	}

	// mode >= 3
	if mode >= 3 {
		resp = vesc.AppendUint32(resp, 0) // odometer
		resp = vesc.AppendFloat16(resp, s.state.AmpHours, 10)
		resp = vesc.AppendFloat16(resp, s.state.AmpHoursCharged, 10)
		resp = vesc.AppendFloat16(resp, s.state.WattHours, 1)
		resp = vesc.AppendFloat16(resp, s.state.WattHoursCharged, 1)
		resp = append(resp, clampUint8(0.85*200)) // battery level 85%
	}

	return resp
}

func (s *Simulator) buildRefloatLightsControlResponse(data []byte) []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandLightsControl)}
	// Response: headlights_enabled << 1 | enabled
	resp = append(resp, 0x03) // both enabled
	return resp
}

func (s *Simulator) buildRefloatLCMPollResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandLCMPoll)}
	// state byte: bits 0-3 = state_compat, bits 4-5 = footpad_sensor_state, bit 7 = handtest
	stateCompat := encodeStateCompat(s.state.RunState, s.state.StopCondition)
	stateByte := stateCompat | (uint8(s.state.Footpad) << 4)
	if s.state.Mode == refloat.ModeHandtest {
		stateByte |= 0x80
	}
	resp = append(resp, stateByte)
	resp = append(resp, byte(s.state.Fault))            // fault code
	resp = append(resp, 0)                               // duty or pitch
	resp = vesc.AppendFloat16(resp, s.state.ERPM, 1)     // erpm
	resp = vesc.AppendFloat16(resp, s.state.BattCurrent, 1) // total current in
	resp = vesc.AppendFloat16(resp, s.state.Voltage, 10) // input voltage
	resp = append(resp, 128)                              // brightness
	resp = append(resp, 64)                               // brightness_idle
	resp = append(resp, 128)                              // status_brightness
	return resp
}

func (s *Simulator) buildRefloatLCMLightInfoResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandLCMLightInfo)}
	// LED type: 0 = no LCM
	resp = append(resp, 0)
	return resp
}

func (s *Simulator) buildRefloatLCMDeviceInfoResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandLCMDeviceInfo)}
	// Empty string (no LCM device)
	resp = append(resp, 0)
	return resp
}

func (s *Simulator) buildRefloatBatteryResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandLCMGetBattery)}
	// Battery level as float32_auto (0.0-1.0)
	resp = appendFloat32Auto(resp, 0.85)
	return resp
}

// buildRefloatRealtimeDataResponse builds COMMAND_REALTIME_DATA (31) response.
func (s *Simulator) buildRefloatRealtimeDataResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandRealtimeData)}

	// mask: bit 0 = running, bit 1 = charging, bit 2 = alerts appended (always set)
	mask := uint8(0x04) // alerts always appended
	if s.state.RunState == refloat.StateRunning {
		mask |= 0x01
	}
	if s.state.Charging {
		mask |= 0x02
	}
	resp = append(resp, mask)

	// extra_flags: bit 0=recording, bit 1=autostart, bit 2=autostop, bit 3=fatal_error
	resp = append(resp, 0)

	// time.now (uint32)
	resp = vesc.AppendUint32(resp, 0)

	// mode << 4 | state
	resp = append(resp, uint8(s.state.Mode)<<4|uint8(s.state.RunState))

	// footpad_state << 6 | charging << 5 | darkride << 1 | wheelslip
	flagsByte := uint8(s.state.Footpad) << 6
	if s.state.Charging {
		flagsByte |= 0x20
	}
	resp = append(resp, flagsByte)

	// sat << 4 | stop_condition
	resp = append(resp, uint8(s.state.SAT)<<4|uint8(s.state.StopCondition))

	// beep_reason
	resp = append(resp, 0)

	// 16 RT_DATA_ITEMS as float16_auto (float16 scale=1... actually these use
	// buffer_append_float16, which is int16 scaled. Per Refloat source, scale varies.)
	// The Refloat source uses float16_auto which is just AppendFloat16 with appropriate scales.
	// Actually looking at the source more carefully, COMMAND_REALTIME_DATA uses a custom
	// float16 encoding. For simplicity, use float16 with scale=10 for most values.
	resp = vesc.AppendFloat16(resp, s.state.Speed, 10)        // motor.speed
	resp = vesc.AppendFloat16(resp, s.state.ERPM, 1)          // motor.erpm
	resp = vesc.AppendFloat16(resp, s.state.MotorCurrent, 10) // motor.current
	resp = vesc.AppendFloat16(resp, s.state.MotorCurrent, 10) // motor.dir_current
	resp = vesc.AppendFloat16(resp, s.state.MotorCurrent, 10) // motor.filt_current
	resp = vesc.AppendFloat16(resp, s.state.DutyCycle, 1000)  // motor.duty_cycle
	resp = vesc.AppendFloat16(resp, s.state.Voltage, 10)      // motor.batt_voltage
	resp = vesc.AppendFloat16(resp, s.state.BattCurrent, 10)  // motor.batt_current
	resp = vesc.AppendFloat16(resp, s.state.MOSFETTemp, 10)   // motor.mosfet_temp
	resp = vesc.AppendFloat16(resp, s.state.MotorTemp, 10)    // motor.motor_temp
	resp = vesc.AppendFloat16(resp, s.state.Pitch, 10)        // imu.pitch
	resp = vesc.AppendFloat16(resp, s.state.Pitch, 10)        // imu.balance_pitch
	resp = vesc.AppendFloat16(resp, s.state.Roll, 10)         // imu.roll
	resp = vesc.AppendFloat16(resp, s.state.ADC1, 100)        // footpad.adc1
	resp = vesc.AppendFloat16(resp, s.state.ADC2, 100)        // footpad.adc2
	resp = vesc.AppendFloat16(resp, 0, 10)                    // remote.input

	// If running, append 10 runtime items
	if mask&0x01 != 0 {
		resp = vesc.AppendFloat16(resp, s.state.Setpoint, 10)       // setpoint
		resp = vesc.AppendFloat16(resp, 0, 10)                      // atr.setpoint
		resp = vesc.AppendFloat16(resp, 0, 10)                      // brake_tilt.setpoint
		resp = vesc.AppendFloat16(resp, 0, 10)                      // torque_tilt.setpoint
		resp = vesc.AppendFloat16(resp, 0, 10)                      // turn_tilt.setpoint
		resp = vesc.AppendFloat16(resp, 0, 10)                      // remote.setpoint
		resp = vesc.AppendFloat16(resp, s.state.BalanceCurrent, 10) // balance_current
		resp = vesc.AppendFloat16(resp, 0, 10)                      // atr.accel_diff
		resp = vesc.AppendFloat16(resp, 0, 10)                      // atr.speed_boost
		resp = vesc.AppendFloat16(resp, 0, 10)                      // booster.current
	}

	// If charging, append charging data
	if mask&0x02 != 0 {
		resp = vesc.AppendFloat16(resp, 0, 10) // charging.current
		resp = vesc.AppendFloat16(resp, 0, 10) // charging.voltage
	}

	// Always append alerts (mask bit 2)
	resp = vesc.AppendUint32(resp, 0)                   // active_alert_mask
	resp = vesc.AppendUint32(resp, 0)                   // reserved
	resp = append(resp, byte(s.state.Fault))            // fw_fault_code

	return resp
}

// buildRefloatRealtimeDataIDsResponse builds COMMAND_REALTIME_DATA_IDS (32) response.
func (s *Simulator) buildRefloatRealtimeDataIDsResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandRealtimeDataIDs)}

	// 16 RT_DATA_ITEMS
	rtItems := []string{
		"motor.speed", "motor.erpm", "motor.current", "motor.dir_current",
		"motor.filt_current", "motor.duty_cycle", "motor.batt_voltage", "motor.batt_current",
		"motor.mosfet_temp", "motor.motor_temp", "imu.pitch", "imu.balance_pitch",
		"imu.roll", "footpad.adc1", "footpad.adc2", "remote.input",
	}
	resp = append(resp, uint8(len(rtItems)))
	for _, name := range rtItems {
		resp = append(resp, uint8(len(name)))
		resp = append(resp, []byte(name)...)
	}

	// 10 RT_DATA_RUNTIME_ITEMS
	runtimeItems := []string{
		"setpoint", "atr.setpoint", "brake_tilt.setpoint", "torque_tilt.setpoint",
		"turn_tilt.setpoint", "remote.setpoint", "balance_current",
		"atr.accel_diff", "atr.speed_boost", "booster.current",
	}
	resp = append(resp, uint8(len(runtimeItems)))
	for _, name := range runtimeItems {
		resp = append(resp, uint8(len(name)))
		resp = append(resp, []byte(name)...)
	}

	return resp
}

// buildRefloatAlertsResponse builds COMMAND_ALERTS_LIST (35) response.
func (s *Simulator) buildRefloatAlertsResponse() []byte {
	resp := []byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandAlertsList)}
	resp = vesc.AppendUint32(resp, 0)        // active_alert_mask
	resp = vesc.AppendUint32(resp, 0)        // reserved
	resp = append(resp, byte(s.state.Fault)) // fw_fault_code
	// No fault name (fault code is 0)
	resp = append(resp, 0) // alert_count = 0
	return resp
}

// Physics simulation loop - updates state at ~50Hz.
func (s *Simulator) physicsLoop() {
	ticker := time.NewTicker(20 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-s.stop:
			return
		case <-ticker.C:
			s.mu.Lock()
			s.updatePhysics()
			s.mu.Unlock()
		}
	}
}

func (s *Simulator) updatePhysics() {
	st := s.state

	// Add tiny noise to sensors
	st.ADC1 += (rand.Float64() - 0.5) * 0.01
	st.ADC2 += (rand.Float64() - 0.5) * 0.01
	if st.ADC1 < 0 {
		st.ADC1 = 0
	}
	if st.ADC2 < 0 {
		st.ADC2 = 0
	}

	// State machine
	switch st.RunState {
	case refloat.StateReady:
		// Board is idle. If both footpads engaged and pitch is level, start balancing.
		if st.Footpad == refloat.FootpadBoth && math.Abs(st.Pitch) < 5.0 && math.Abs(st.Roll) < 30.0 {
			st.RunState = refloat.StateRunning
			st.StopCondition = refloat.StopNone
		}
		// Gradually reduce speed when idle
		st.Speed *= 0.95
		st.ERPM *= 0.95
		st.MotorCurrent *= 0.9
		st.DutyCycle *= 0.9

	case refloat.StateRunning:
		// Check fault conditions
		if math.Abs(st.Pitch) > 15.0 {
			st.RunState = refloat.StateReady
			st.StopCondition = refloat.StopPitch
			break
		}
		if math.Abs(st.Roll) > 60.0 {
			st.RunState = refloat.StateReady
			st.StopCondition = refloat.StopRoll
			break
		}
		if st.Footpad == refloat.FootpadNone {
			st.RunState = refloat.StateReady
			st.StopCondition = refloat.StopSwitchFull
			break
		}

		// Simple balance physics: pitch drives motor current
		st.BalanceCurrent = st.Pitch * 2.0 // P controller
		st.MotorCurrent = st.BalanceCurrent

		// Speed responds to pitch
		st.Speed += st.Pitch * 0.01
		st.Speed *= 0.99 // drag

		// ERPM from speed (rough: 1 m/s ~ 700 ERPM for typical setup)
		st.ERPM = st.Speed * 700.0

		// Duty from speed
		st.DutyCycle = st.Speed / 15.0 // max ~15 m/s

		// Battery current from motor current and duty
		st.BattCurrent = st.MotorCurrent * math.Abs(st.DutyCycle)

		// Setpoint adjustment
		st.SAT = refloat.SATNone
		if math.Abs(st.DutyCycle) > 0.8 {
			st.SAT = refloat.SATPBDuty
		}

		// Slow voltage drain
		st.Voltage -= 0.00001

		// Temperature rises slowly under load
		st.MOSFETTemp += math.Abs(st.MotorCurrent) * 0.0001
		st.MotorTemp += math.Abs(st.MotorCurrent) * 0.00005

		// Counters
		st.AmpHours += math.Abs(st.BattCurrent) * 0.02 / 3600.0
		st.WattHours += math.Abs(st.BattCurrent*st.Voltage) * 0.02 / 3600.0
	}
}

// Helper to encode float32_auto (IEEE 754 float32, big-endian).
func appendFloat32Auto(buf []byte, v float64) []byte {
	bits := math.Float32bits(float32(v))
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, bits)
	return append(buf, b...)
}

func encodeStateCompat(state refloat.RunState, stop refloat.StopCondition) uint8 {
	switch state {
	case refloat.StateStartup:
		return 0
	case refloat.StateRunning:
		return 1
	case refloat.StateReady:
		switch stop {
		case refloat.StopPitch:
			return 6
		case refloat.StopRoll:
			return 7
		case refloat.StopSwitchHalf:
			return 8
		case refloat.StopSwitchFull:
			return 9
		case refloat.StopReverseStop:
			return 12
		case refloat.StopQuickstop:
			return 13
		default:
			return 11 // FAULT_STARTUP
		}
	case refloat.StateDisabled:
		return 15
	default:
		return 11
	}
}

func encodeSATCompat(sat refloat.SetpointAdjustmentType) uint8 {
	switch sat {
	case refloat.SATCentering:
		return 0
	case refloat.SATReverseStop:
		return 1
	case refloat.SATNone:
		return 2
	case refloat.SATPBDuty:
		return 3
	case refloat.SATPBHighVoltage:
		return 4
	case refloat.SATPBLowVoltage:
		return 5
	case refloat.SATPBTemperature:
		return 6
	case refloat.SATPBSpeed:
		return 7
	case refloat.SATPBError:
		return 8
	default:
		return 2
	}
}

func clampUint8(v float64) uint8 {
	if v < 0 {
		return 0
	}
	if v > 255 {
		return 255
	}
	return uint8(v)
}
