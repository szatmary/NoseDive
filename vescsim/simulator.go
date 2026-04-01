package main

import (
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"math"
	"math/rand"
	"net"
	"sync"
	"sync/atomic"
	"time"

)

// Simulator emulates a VESC running the Refloat package over TCP and/or BLE.
type Simulator struct {
	listener   net.Listener
	mu         sync.Mutex
	wg         sync.WaitGroup // tracks background goroutines for clean shutdown
	state      *BoardState
	profile    *Profile // Optional board profile
	canDevices []CANDevice    // Devices on the simulated CAN bus
	running    atomic.Bool
	stop       chan struct{}
	bleName    string // BLE device name (empty = no BLE)
	webAddr    string // Web GUI address
	statePath  string // Path to persisted state JSON
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
	Fault        FaultCode

	// Motor
	ERPM         float64
	DutyCycle    float64
	MotorCurrent float64
	BattCurrent  float64
	MOSFETTemp   float64
	MotorTemp    float64
	Speed        float64 // m/s

	// Refloat
	RunState      RunState
	Mode          Mode
	SAT           SetpointAdjustmentType
	StopCondition StopCondition
	Footpad       FootpadState
	Charging      bool

	// IMU
	Pitch     float64
	Roll      float64
	PrevPitch float64 // previous tick pitch for gyro rate
	PrevRoll  float64 // previous tick roll for gyro rate

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
	Distance         float64 // meters, accumulated
	Odometer         float64 // lifetime meters

	// Stats (accumulated during ride)
	SpeedMax     float64
	SpeedSum     float64
	PowerMax     float64
	PowerSum     float64
	CurrentMax   float64
	CurrentSum   float64
	MOSFETTempMax float64
	MotorTempMax  float64
	StatSamples  uint32 // number of samples for averages
	UptimeMs     uint32 // milliseconds since start

	// Battery config
	BatterySeriesCells int
	BatteryVoltageMin  float64
	BatteryVoltageMax  float64

	// Refloat package info
	HasRefloat bool   // Whether Refloat is installed
	PkgName    string
	PkgMajor   uint8
	PkgMinor   uint8
	PkgPatch   uint8
	PkgSuffix  string

	// Motor detection results (populated from profile or defaults)
	MotorResistance  float64 // ohms
	MotorInductance  float64 // henries
	MotorFluxLinkage float64 // weber
	HallSensorTable  [8]uint8
	MotorPolePairs   int
	WheelCircumM     float64 // wheel circumference in meters

	// Config data
	ConfigXML  []byte // Refloat custom config XML
	ConfigData []byte // Serialized Refloat custom config binary
	MCConf     []byte // Motor controller config blob
	AppConf    []byte // App config blob

	// Package install buffers (accumulated during write chunks)
	qmlWriteBuf  []byte
	lispWriteBuf []byte

	// LCM (LED Controller Module)
	LCMPresent       bool
	LCMLedType       uint8  // 0=none, 1=RGB, 2=RGBW
	LCMHeadlightOn   bool
	LCMTaillightOn   bool
}

// DefaultBoardState returns a realistic idle board state.
func DefaultBoardState() *BoardState {
	bs := &BoardState{
		FWMajor:      6,
		FWMinor:      5,
		HWName:       "60_MK6",
		ControllerID: 0,
		Voltage:      63.0, // ~15S fully charged
		MOSFETTemp:   28.0,
		MotorTemp:    25.0,
		RunState:     StateReady,
		Mode:         ModeNormal,
		StopCondition: StopNone,
		Footpad:      FootpadNone,
		Pitch:        0.0,
		Roll:         0.0,
		ADC1:         0.0,
		ADC2:         0.0,
		HasRefloat:   true,
		PkgName:      "Refloat",
		PkgMajor:     2,
		PkgMinor:     0,
		PkgPatch:     1,
		PkgSuffix:    "sim",
		MotorResistance:  0.088,
		MotorInductance:  0.000233,
		MotorFluxLinkage: 0.028,
		HallSensorTable:  [8]uint8{255, 1, 3, 2, 5, 6, 4, 255},
		MotorPolePairs:   15,
		WheelCircumM:     0.8778, // 11" tire
		BatterySeriesCells: 15,
		BatteryVoltageMin:  42.0,  // 15S × 2.8V
		BatteryVoltageMax:  63.0,  // 15S × 4.2V
		LCMPresent:       true,
		LCMLedType:       2,      // RGBW
		LCMHeadlightOn:   true,
		LCMTaillightOn:   true,
		ConfigXML:        compressConfigXML(refloatConfigXML),
		MCConf:           generateDefaultMCConf(),
		AppConf:          generateDefaultAppConf(),
	}

	// Generate default binary config from XML
	refloatSig := uint32(CRC16(refloatConfigXML))
	bs.ConfigData = generateDefaultConfig(refloatConfigXML, refloatSig)

	// Generate a fake UUID
	copy(bs.UUID[:], []byte{0x4e, 0x6f, 0x73, 0x65, 0x44, 0x69, 0x76, 0x65, 0x53, 0x49, 0x4d, 0x31})

	return bs
}

// New creates a new simulator with default board state.
// Includes a VESC Express on the CAN bus by default.
func New() *Simulator {
	return &Simulator{
		state:      DefaultBoardState(),
		canDevices: defaultCANBus(),
		stop:       make(chan struct{}),
	}
}

// defaultCANBus returns the standard CAN bus devices: VESC Express + BMS.
func defaultCANBus() []CANDevice {
	return []CANDevice{DefaultVESCExpress(), DefaultVESCBMS()}
}

// NewWithProfile creates a simulator initialized from a board profile.
func NewWithProfile(p *Profile) *Simulator {
	bs := DefaultBoardState()

	// Apply profile values
	bs.FWMajor = p.Controller.Firmware.Major
	bs.FWMinor = p.Controller.Firmware.Minor
	bs.HWName = p.Controller.Hardware
	bs.Voltage = p.Battery.VoltageMax // Start fully charged

	// Motor detection values from profile
	bs.MotorResistance = p.Motor.Resistance
	bs.MotorInductance = p.Motor.Inductance
	bs.MotorFluxLinkage = p.Motor.FluxLinkage
	bs.MotorPolePairs = p.Motor.PolePairs
	if len(p.Motor.HallSensorTable) == 8 {
		for i, v := range p.Motor.HallSensorTable {
			bs.HallSensorTable[i] = uint8(v)
		}
	}

	// Battery from profile
	bs.BatterySeriesCells = p.Battery.SeriesCells
	bs.BatteryVoltageMin = p.Battery.VoltageMin
	bs.BatteryVoltageMax = p.Battery.VoltageMax

	// Wheel from profile
	if p.Wheel.CircumferenceM > 0 {
		bs.WheelCircumM = p.Wheel.CircumferenceM
	}

	s := &Simulator{
		state:      bs,
		profile:    p,
		canDevices: defaultCANBus(),
		stop:       make(chan struct{}),
	}
	return s
}

// Profile returns the loaded board profile, if any.
func (s *Simulator) Profile() *Profile {
	return s.profile
}

// SetHasRefloat toggles whether Refloat is installed on the simulated 
func (s *Simulator) SetHasRefloat(installed bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state.HasRefloat = installed
	if !installed {
		s.state.PkgName = ""
		s.state.PkgMajor = 0
		s.state.PkgMinor = 0
		s.state.PkgPatch = 0
		s.state.PkgSuffix = ""
	}
}

// Start begins listening on the given address.
func (s *Simulator) Start(addr string) error {
	var err error
	s.listener, err = net.Listen("tcp", addr)
	if err != nil {
		return fmt.Errorf("listen on %s: %w", addr, err)
	}
	s.running.Store(true)

	// Start physics simulation
	s.wg.Add(2)
	go func() {
		defer s.wg.Done()
		s.physicsLoop()
	}()
	go func() {
		defer s.wg.Done()
		s.acceptLoop()
	}()
	return nil
}

// Addr returns the listener address.
func (s *Simulator) Addr() string {
	if s.listener == nil {
		return ""
	}
	return s.listener.Addr().String()
}

// Stop shuts down the simulator and waits for background goroutines to finish.
func (s *Simulator) Stop() {
	s.running.Store(false)
	close(s.stop)
	if s.listener != nil {
		s.listener.Close()
	}
	s.wg.Wait()
}

// State returns a snapshot of the current board state.
func (s *Simulator) State() BoardState {
	s.mu.Lock()
	defer s.mu.Unlock()
	return *s.state
}

// SetFootpad simulates stepping on/off the footpad.
func (s *Simulator) SetFootpad(state FootpadState) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state.Footpad = state
	s.state.ADC1 = 0.0
	s.state.ADC2 = 0.0
	if state == FootpadLeft || state == FootpadBoth {
		s.state.ADC1 = 3.0
	}
	if state == FootpadRight || state == FootpadBoth {
		s.state.ADC2 = 3.0
	}
}

// SetPitch simulates tilting the 
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

func (s *Simulator) SetUUID(uuid [12]byte) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state.UUID = uuid
}

func (s *Simulator) SetRunState(state RunState) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state.RunState = state
}

const maxConnections = 100

func (s *Simulator) acceptLoop() {
	connSem := make(chan struct{}, maxConnections)
	for s.running.Load() {
		conn, err := s.listener.Accept()
		if err != nil {
			if s.running.Load() {
				log.Printf("simulator: accept error: %v", err)
			}
			return
		}
		select {
		case connSem <- struct{}{}:
			go func(c net.Conn) {
				defer func() { <-connSem }()
				s.handleConn(c)
			}(conn)
		default:
			log.Printf("simulator: max connections reached, rejecting %s", conn.RemoteAddr())
			conn.Close()
		}
	}
}

func (s *Simulator) handleConn(conn net.Conn) {
	defer conn.Close()
	log.Printf("simulator: client connected from %s", conn.RemoteAddr())

	for s.running.Load() {
		payload, err := DecodePacket(conn)
		if err != nil {
			if err != io.EOF {
				log.Printf("simulator: decode error: %v", err)
			}
			return
		}

		resp := s.HandleCommand(payload)
		if resp != nil {
			logTx(resp)
			pkt, err := EncodePacket(resp)
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
	logRx(payload)
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.handleCommandLocked(payload)
}

// handleCommandLocked does the actual dispatch. Caller must hold s.mu.
func (s *Simulator) handleCommandLocked(payload []byte) []byte {
	cmd := CommPacketID(payload[0])

	switch cmd {
	case CommFWVersion:
		return s.buildFWVersionResponse()
	case CommGetValues:
		return s.buildGetValuesResponse()
	case CommGetValuesSetup:
		return s.buildGetValuesSetupResponse()
	case CommGetValuesSelective, CommGetValuesSetupSelective:
		// Return full values for selective requests
		return s.buildGetValuesResponse()
	case CommAlive:
		return nil // no response needed
	case CommSetDuty, CommSetCurrent, CommSetCurrentBrake,
		CommSetRPM, CommSetPos, CommSetHandbrake,
		CommSetCurrentRel:
		return nil // motor control commands - no response
	case CommGetMCConf, CommGetMCConfDefault:
		return s.buildGetMCConfResponse()
	case CommSetMCConf:
		if len(payload) > 1 {
			s.state.MCConf = make([]byte, len(payload)-1)
			copy(s.state.MCConf, payload[1:])
			log.Printf("simulator: MCConf updated (%d bytes)", len(s.state.MCConf))
			s.persistState()
		}
		return []byte{byte(CommSetMCConf)}
	case CommGetAppConf, CommGetAppConfDefault:
		return s.buildGetAppConfResponse()
	case CommSetAppConf:
		if len(payload) > 1 {
			s.state.AppConf = make([]byte, len(payload)-1)
			copy(s.state.AppConf, payload[1:])
			log.Printf("simulator: AppConf updated (%d bytes)", len(s.state.AppConf))
			s.persistState()
		}
		return []byte{byte(CommSetAppConf)}
	case CommGetDecodedPPM:
		return s.buildGetDecodedPPMResponse()
	case CommGetDecodedADC:
		return s.buildGetDecodedADCResponse()
	case CommGetDecodedChuk:
		return s.buildGetDecodedChukResponse()
	case CommGetDecodedBalance:
		// Return empty balance data
		resp := []byte{byte(CommGetDecodedBalance)}
		resp = AppendFloat32(resp, s.state.Pitch, 1000000)
		resp = AppendFloat32(resp, s.state.Roll, 1000000)
		resp = AppendFloat32(resp, 0, 1000000) // diff time
		resp = AppendFloat32(resp, s.state.MotorCurrent, 1000000)
		return resp
	case CommPingCAN:
		return s.buildPingCANResponse()
	case CommForwardCAN:
		// CAN forwarding: [0x22][target_id][inner_command...]
		if len(payload) < 3 {
			return nil
		}
		targetID := payload[1]
		innerPayload := payload[2:]
		// Route to self if target matches our controller ID
		if targetID == s.state.ControllerID {
			return s.handleCommandLocked(innerPayload)
		}
		// Route to CAN device with matching ID
		for _, dev := range s.canDevices {
			if dev.ID() == targetID {
				return dev.HandleCommand(innerPayload)
			}
		}
		log.Printf("simulator: CAN forward to unknown device %d", targetID)
		return nil
	case CommTerminalCmd, CommTerminalCmdSync:
		return s.buildTerminalResponse(payload)
	case CommGetIMUData:
		return s.buildGetIMUDataResponse(payload)
	case CommGetCustomConfigXML:
		return s.buildGetCustomConfigXMLResponse(payload)
	case CommGetCustomConfig, CommGetCustomConfigDefault:
		return s.buildGetCustomConfigResponse(payload)
	case CommSetCustomConfig:
		return s.buildSetCustomConfigResponse(payload)
	case CommGetBatteryCut:
		return s.buildGetBatteryCutResponse()
	case CommSetBatteryCut:
		return nil // accept silently
	case CommGetStats:
		return s.buildGetStatsResponse(payload)
	case CommResetStats:
		return nil // accept silently
	case CommGetQMLUIHW:
		return s.buildQMLUIResponse(CommGetQMLUIHW, payload)
	case CommGetQMLUIApp:
		return s.buildQMLUIResponse(CommGetQMLUIApp, payload)
	case CommSetMCConfTemp, CommSetMCConfTempSetup:
		return nil // accept silently
	case CommGetMCConfTemp:
		return []byte{byte(CommGetMCConfTemp)}
	case CommDetectMotorParam:
		return s.buildDetectMotorParamResponse(payload)
	case CommDetectMotorRL:
		return s.buildDetectMotorRLResponse()
	case CommDetectMotorFlux:
		return s.buildDetectMotorFluxResponse()
	case CommDetectEncoder:
		return s.buildDetectEncoderResponse()
	case CommDetectHallFOC:
		return s.buildDetectHallFOCResponse()
	case CommDetectMotorFluxOpenloop:
		return s.buildDetectMotorFluxOpenloopResponse()
	case CommDetectApplyAllFOC:
		return s.buildDetectApplyAllFOCResponse()
	case CommReboot, CommShutdown:
		log.Printf("simulator: received command 0x%02x (ignored)", cmd)
		return nil
	case CommAppDisableOutput:
		return nil // accept silently
	case CommSetOdometer:
		return nil
	case CommBMSGetValues:
		// Return minimal BMS response
		return []byte{byte(CommBMSGetValues)}
	case CommSetChuckData:
		return nil
	case CommFWInfo:
		return s.buildFWVersionResponse() // same format
	case CommCustomAppData:
		if len(payload) < 2 {
			return nil
		}
		return s.handleCustomAppData(payload[1:])
	case CommEraseNewApp:
		// Simulate erasing custom app (Refloat)
		log.Printf("simulator: erasing custom app")
		s.state.HasRefloat = false
		return []byte{byte(CommEraseNewApp)}
	case CommWriteNewAppData:
		// Simulate writing new custom app data — after all chunks, Refloat is installed
		// In real VESC, this receives chunks of firmware binary. We simulate completion immediately.
		if !s.state.HasRefloat {
			log.Printf("simulator: installing Refloat package")
			s.state.HasRefloat = true
			s.state.PkgName = "Refloat"
			s.state.PkgMajor = 2
			s.state.PkgMinor = 0
			s.state.PkgPatch = 1
			s.state.PkgSuffix = "sim"
		}
		return []byte{byte(CommWriteNewAppData)}
	case CommQmluiErase:
		log.Printf("simulator: erase QML")
		s.state.qmlWriteBuf = nil
		return []byte{byte(cmd), 1}
	case CommLispEraseCode:
		log.Printf("simulator: erase Lisp")
		s.state.lispWriteBuf = nil
		return []byte{byte(cmd), 1}
	case CommQmluiWrite:
		offset := int32(0)
		if len(payload) >= 5 {
			idx := 1
			offset = GetInt32(payload, &idx)
			data := payload[5:]
			// Grow buffer to fit
			end := int(offset) + len(data)
			for len(s.state.qmlWriteBuf) < end {
				s.state.qmlWriteBuf = append(s.state.qmlWriteBuf, 0)
			}
			copy(s.state.qmlWriteBuf[offset:], data)
		}
		resp := []byte{byte(cmd), 1}
		resp = AppendInt32(resp, offset)
		log.Printf("simulator: write QML offset=%d len=%d total=%d", offset, len(payload)-5, len(s.state.qmlWriteBuf))
		return resp
	case CommLispWriteCode:
		offset := int32(0)
		if len(payload) >= 5 {
			idx := 1
			offset = GetInt32(payload, &idx)
			data := payload[5:]
			end := int(offset) + len(data)
			for len(s.state.lispWriteBuf) < end {
				s.state.lispWriteBuf = append(s.state.lispWriteBuf, 0)
			}
			copy(s.state.lispWriteBuf[offset:], data)
		}
		resp := []byte{byte(cmd), 1}
		resp = AppendInt32(resp, offset)
		log.Printf("simulator: write Lisp offset=%d len=%d total=%d", offset, len(payload)-5, len(s.state.lispWriteBuf))
		return resp
	case CommLispSetRunning:
		running := false
		if len(payload) > 1 {
			running = payload[1] != 0
		}
		log.Printf("simulator: Lisp set running=%v (qml=%d bytes, lisp=%d bytes)",
			running, len(s.state.qmlWriteBuf), len(s.state.lispWriteBuf))
		if running {
			s.state.HasRefloat = true
			s.state.PkgName = "Refloat"
			s.state.PkgMajor = 2
			s.state.PkgMinor = 0
			s.state.PkgPatch = 1
			s.state.PkgSuffix = "sim"
			// Validate CRC of written data
			if len(s.state.lispWriteBuf) > 0 {
				crc := CRC16(s.state.lispWriteBuf)
				log.Printf("simulator: Refloat installed — Lisp CRC=0x%04X, QML=%d bytes", crc, len(s.state.qmlWriteBuf))
			}
		}
		return []byte{byte(CommLispSetRunning), 1} // 1 = ok
	case CommLispGetStats:
		// Return minimal Lisp stats
		resp := []byte{byte(CommLispGetStats)}
		resp = AppendFloat32Auto(resp, 0)    // cpu_use
		resp = AppendFloat32Auto(resp, 4096) // heap_use
		resp = AppendFloat32Auto(resp, 8192) // mem_use
		return resp
	case CommLispReplCmd, CommLispStreamCode:
		return nil // accept silently
	case CommSetBLEName, CommSetBLEPin, CommSetCANMode:
		return nil // accept silently
	case CommGetIMUCalibration:
		resp := []byte{byte(CommGetIMUCalibration)}
		// accel offsets (3 x float32) + gyro offsets (3 x float32)
		for i := 0; i < 6; i++ {
			resp = AppendFloat32(resp, 0, 1000)
		}
		return resp
	default:
		log.Printf("simulator: unhandled command 0x%02x (%d)", cmd, cmd)
		return nil
	}
}

func (s *Simulator) handleCustomAppData(data []byte) []byte {
	if len(data) < 2 || data[0] != PackageMagic {
		return nil
	}
	// No response if Refloat is not installed
	if !s.state.HasRefloat {
		return nil
	}

	cmd := CommandID(data[1])
	switch cmd {
	case CommandInfo:
		return s.buildRefloatInfoResponse()
	case CommandGetRTData:
		return s.buildRefloatRTDataResponse()
	case CommandGetAllData:
		mode := uint8(0)
		if len(data) > 2 {
			mode = data[2]
		}
		return s.buildRefloatAllDataResponse(mode)
	case CommandRTTune:
		return nil // fire-and-forget: applies tune values at runtime
	case CommandTuneDefaults:
		return nil // fire-and-forget: resets tune to defaults
	case CommandTuneOther:
		return nil // fire-and-forget: applies startup/tilt tune
	case CommandTuneTilt:
		return nil // fire-and-forget: applies tilt/duty tune
	case CommandCfgSave:
		log.Printf("simulator: refloat config save requested")
		return nil
	case CommandCfgRestore:
		log.Printf("simulator: refloat config restore requested")
		return nil
	case CommandRCMove:
		return nil // fire-and-forget
	case CommandBooster:
		return nil // fire-and-forget
	case CommandPrintInfo:
		return nil // no-op in Refloat source
	case CommandExperiment:
		return nil // no-op
	case CommandLock:
		return nil // fire-and-forget
	case CommandHandtest:
		if len(data) > 2 {
			if data[2] != 0 {
				s.state.Mode = ModeHandtest
			} else {
				s.state.Mode = ModeNormal
			}
		}
		return nil // fire-and-forget
	case CommandFlywheel:
		// Requires specific flag format; simplified for sim
		if s.state.Mode == ModeFlywheel {
			s.state.Mode = ModeNormal
		} else {
			s.state.Mode = ModeFlywheel
		}
		return nil // fire-and-forget
	case CommandLightsControl:
		return s.buildRefloatLightsControlResponse(data)
	case CommandLCMPoll:
		return s.buildRefloatLCMPollResponse()
	case CommandLCMLightInfo:
		return s.buildRefloatLCMLightInfoResponse()
	case CommandLCMLightCtrl:
		return nil // fire-and-forget
	case CommandLCMDeviceInfo:
		return s.buildRefloatLCMDeviceInfoResponse()
	case CommandChargingState:
		return nil // fire-and-forget: updates charging state from LCM
	case CommandLCMGetBattery:
		return s.buildRefloatBatteryResponse()
	case CommandRealtimeData:
		return s.buildRefloatRealtimeDataResponse()
	case CommandRealtimeDataIDs:
		return s.buildRefloatRealtimeDataIDsResponse()
	case CommandAlertsList:
		return s.buildRefloatAlertsResponse()
	case CommandAlertsControl:
		return nil // fire-and-forget
	case CommandDataRecordReq:
		return nil // no data recording in sim
	default:
		log.Printf("simulator: unhandled refloat command %d", cmd)
		return nil
	}
}

func (s *Simulator) buildRefloatInfoResponse() []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandInfo)}
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
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandGetRTData)}

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
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandGetAllData)}

	// mode echo
	resp = append(resp, mode)

	// balance_current (float16, scale=10)
	resp = AppendFloat16(resp, s.state.BalanceCurrent, 10)
	// balance_pitch (float16, scale=10)
	resp = AppendFloat16(resp, s.state.Pitch, 10)
	// roll (float16, scale=10)
	resp = AppendFloat16(resp, s.state.Roll, 10)

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
	resp = AppendFloat16(resp, s.state.Pitch, 10)

	// booster current + 128
	resp = append(resp, 128)

	// battery voltage (float16, scale=10)
	resp = AppendFloat16(resp, s.state.Voltage, 10)

	// erpm (int16)
	resp = AppendInt16(resp, int16(s.state.ERPM))

	// speed (float16, scale=10)
	resp = AppendFloat16(resp, s.state.Speed, 10)

	// motor current (float16, scale=10)
	resp = AppendFloat16(resp, s.state.MotorCurrent, 10)

	// battery current (float16, scale=10)
	resp = AppendFloat16(resp, s.state.BattCurrent, 10)

	// duty * 100 + 128
	resp = append(resp, clampUint8(s.state.DutyCycle*100+128))

	// foc_id (skip)
	resp = append(resp, 222) // magic for unavailable

	// mode >= 2
	if mode >= 2 {
		resp = appendFloat32Auto(resp, s.state.Distance)
		resp = append(resp, clampUint8(s.state.MOSFETTemp*2))
		resp = append(resp, clampUint8(s.state.MotorTemp*2))
		resp = append(resp, 0) // reserved
	}

	// mode >= 3
	if mode >= 3 {
		resp = AppendUint32(resp, uint32(s.state.Odometer))
		resp = AppendFloat16(resp, s.state.AmpHours, 10)
		resp = AppendFloat16(resp, s.state.AmpHoursCharged, 10)
		resp = AppendFloat16(resp, s.state.WattHours, 1)
		resp = AppendFloat16(resp, s.state.WattHoursCharged, 1)
		resp = append(resp, clampUint8(s.state.BatteryPercent()*200))
	}

	return resp
}

func (s *Simulator) buildRefloatLightsControlResponse(data []byte) []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandLightsControl)}
	// Parse incoming control byte: data = [magic][cmd][ctrl_byte...]
	if len(data) >= 3 {
		ctrl := data[2]
		s.state.LCMTaillightOn = ctrl&0x01 != 0
		s.state.LCMHeadlightOn = ctrl&0x02 != 0
	}
	// Response: headlights_enabled << 1 | taillights_enabled
	var flags uint8
	if s.state.LCMTaillightOn {
		flags |= 0x01
	}
	if s.state.LCMHeadlightOn {
		flags |= 0x02
	}
	resp = append(resp, flags)
	return resp
}

func (s *Simulator) buildRefloatLCMPollResponse() []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandLCMPoll)}
	stateCompat := encodeStateCompat(s.state.RunState, s.state.StopCondition)
	stateByte := stateCompat | (uint8(s.state.Footpad) << 4)
	if s.state.Mode == ModeHandtest {
		stateByte |= 0x80
	}
	resp = append(resp, stateByte)
	resp = append(resp, byte(s.state.Fault))
	resp = append(resp, clampUint8(s.state.DutyCycle*100+128)) // duty encoded
	resp = AppendFloat16(resp, s.state.ERPM, 1)
	resp = AppendFloat16(resp, s.state.BattCurrent, 1)
	resp = AppendFloat16(resp, s.state.Voltage, 10)
	resp = append(resp, 128) // brightness
	resp = append(resp, 64)  // brightness_idle
	resp = append(resp, 128) // status_brightness
	return resp
}

func (s *Simulator) buildRefloatLCMLightInfoResponse() []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandLCMLightInfo)}
	if s.state.LCMPresent {
		resp = append(resp, s.state.LCMLedType) // 1=RGB, 2=RGBW
	} else {
		resp = append(resp, 0) // no LCM
	}
	return resp
}

func (s *Simulator) buildRefloatLCMDeviceInfoResponse() []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandLCMDeviceInfo)}
	if s.state.LCMPresent {
		name := "NoseDive LCM Sim"
		resp = append(resp, []byte(name)...)
		resp = append(resp, 0)
	} else {
		resp = append(resp, 0)
	}
	return resp
}

func (s *Simulator) buildRefloatBatteryResponse() []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandLCMGetBattery)}
	resp = appendFloat32Auto(resp, s.state.BatteryPercent())
	return resp
}

// buildRefloatRealtimeDataResponse builds COMMAND_REALTIME_DATA (31) response.
func (s *Simulator) buildRefloatRealtimeDataResponse() []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandRealtimeData)}

	// mask: bit 0 = running, bit 1 = charging, bit 2 = alerts appended (always set)
	mask := uint8(0x04) // alerts always appended
	if s.state.RunState == StateRunning {
		mask |= 0x01
	}
	if s.state.Charging {
		mask |= 0x02
	}
	resp = append(resp, mask)

	// extra_flags: bit 0=recording, bit 1=autostart, bit 2=autostop, bit 3=fatal_error
	resp = append(resp, 0)

	// time.now (uint32)
	resp = AppendUint32(resp, 0)

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
	resp = AppendFloat16(resp, s.state.Speed, 10)        // motor.speed
	resp = AppendFloat16(resp, s.state.ERPM, 1)          // motor.erpm
	resp = AppendFloat16(resp, s.state.MotorCurrent, 10) // motor.current
	resp = AppendFloat16(resp, s.state.MotorCurrent, 10) // motor.dir_current
	resp = AppendFloat16(resp, s.state.MotorCurrent, 10) // motor.filt_current
	resp = AppendFloat16(resp, s.state.DutyCycle, 1000)  // motor.duty_cycle
	resp = AppendFloat16(resp, s.state.Voltage, 10)      // motor.batt_voltage
	resp = AppendFloat16(resp, s.state.BattCurrent, 10)  // motor.batt_current
	resp = AppendFloat16(resp, s.state.MOSFETTemp, 10)   // motor.mosfet_temp
	resp = AppendFloat16(resp, s.state.MotorTemp, 10)    // motor.motor_temp
	resp = AppendFloat16(resp, s.state.Pitch, 10)        // imu.pitch
	resp = AppendFloat16(resp, s.state.Pitch, 10)        // imu.balance_pitch
	resp = AppendFloat16(resp, s.state.Roll, 10)         // imu.roll
	resp = AppendFloat16(resp, s.state.ADC1, 100)        // footpad.adc1
	resp = AppendFloat16(resp, s.state.ADC2, 100)        // footpad.adc2
	resp = AppendFloat16(resp, 0, 10)                    // remote.input

	// If running, append 10 runtime items
	if mask&0x01 != 0 {
		resp = AppendFloat16(resp, s.state.Setpoint, 10)       // setpoint
		resp = AppendFloat16(resp, 0, 10)                      // atr.setpoint
		resp = AppendFloat16(resp, 0, 10)                      // brake_tilt.setpoint
		resp = AppendFloat16(resp, 0, 10)                      // torque_tilt.setpoint
		resp = AppendFloat16(resp, 0, 10)                      // turn_tilt.setpoint
		resp = AppendFloat16(resp, 0, 10)                      // remote.setpoint
		resp = AppendFloat16(resp, s.state.BalanceCurrent, 10) // balance_current
		resp = AppendFloat16(resp, 0, 10)                      // atr.accel_diff
		resp = AppendFloat16(resp, 0, 10)                      // atr.speed_boost
		resp = AppendFloat16(resp, 0, 10)                      // booster.current
	}

	// If charging, append charging data
	if mask&0x02 != 0 {
		resp = AppendFloat16(resp, 0, 10) // charging.current
		resp = AppendFloat16(resp, 0, 10) // charging.voltage
	}

	// Always append alerts (mask bit 2)
	resp = AppendUint32(resp, 0)                   // active_alert_mask
	resp = AppendUint32(resp, 0)                   // reserved
	resp = append(resp, byte(s.state.Fault))            // fw_fault_code

	return resp
}

// buildRefloatRealtimeDataIDsResponse builds COMMAND_REALTIME_DATA_IDS (32) response.
func (s *Simulator) buildRefloatRealtimeDataIDsResponse() []byte {
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandRealtimeDataIDs)}

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
	resp := []byte{byte(CommCustomAppData), PackageMagic, byte(CommandAlertsList)}
	resp = AppendUint32(resp, 0)        // active_alert_mask
	resp = AppendUint32(resp, 0)        // reserved
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

const physTickSec = 0.02 // 50 Hz = 20ms per tick

func (s *Simulator) updatePhysics() {
	st := s.state
	st.UptimeMs += 20

	// Save previous IMU for gyro rate calculation
	st.PrevPitch = st.Pitch
	st.PrevRoll = st.Roll

	// Sensor noise on ADC
	st.ADC1 += (rand.Float64() - 0.5) * 0.02
	st.ADC2 += (rand.Float64() - 0.5) * 0.02
	if st.ADC1 < 0 {
		st.ADC1 = 0
	}
	if st.ADC2 < 0 {
		st.ADC2 = 0
	}

	// ERPM per m/s = polePairs * 60 / wheelCircumM
	if st.MotorPolePairs <= 0 || st.WheelCircumM <= 0 {
		return // skip physics tick if motor/wheel not configured
	}
	erpmPerMPS := float64(st.MotorPolePairs) * 60.0 / st.WheelCircumM

	// State machine
	switch st.RunState {
	case StateReady:
		if st.Footpad == FootpadBoth && math.Abs(st.Pitch) < 5.0 && math.Abs(st.Roll) < 30.0 {
			st.RunState = StateRunning
			st.StopCondition = StopNone
		}
		// Coast to stop
		st.Speed *= 0.92
		st.MotorCurrent *= 0.85
		st.BalanceCurrent *= 0.85
		st.ERPM = st.Speed * erpmPerMPS
		st.DutyCycle = st.Speed / 15.0

	case StateRunning:
		// Fault checks
		if math.Abs(st.Pitch) > 15.0 {
			st.RunState = StateReady
			st.StopCondition = StopPitch
			break
		}
		if math.Abs(st.Roll) > 60.0 {
			st.RunState = StateReady
			st.StopCondition = StopRoll
			break
		}
		if st.Footpad == FootpadNone {
			st.RunState = StateReady
			st.StopCondition = StopSwitchFull
			break
		}

		// PD balance controller: current = Kp*pitch + Kd*pitchRate
		pitchRate := (st.Pitch - st.PrevPitch) / physTickSec
		st.BalanceCurrent = st.Pitch*3.0 + pitchRate*0.15
		// Clamp to realistic motor current limits
		if st.BalanceCurrent > 40 {
			st.BalanceCurrent = 40
		}
		if st.BalanceCurrent < -40 {
			st.BalanceCurrent = -40
		}
		st.MotorCurrent = st.BalanceCurrent

		// Speed: pitch tilts accelerate, with friction + aero drag
		accel := st.Pitch * 0.015                        // tilt → acceleration
		friction := st.Speed * 0.005                      // rolling friction
		aero := st.Speed * math.Abs(st.Speed) * 0.0005   // air drag (quadratic)
		st.Speed += accel - friction - aero

		// Clamp speed to reasonable range
		if st.Speed > 15 {
			st.Speed = 15
		}
		if st.Speed < -8 {
			st.Speed = -8
		}

		st.ERPM = st.Speed * erpmPerMPS
		st.DutyCycle = st.Speed / 15.0
		st.BattCurrent = st.MotorCurrent * math.Abs(st.DutyCycle)

		// Tachometer: accumulate from ERPM
		// ERPM is electrical RPM; tach counts electrical revolutions
		// tach_per_tick = ERPM / 60 * tickSec
		tachDelta := st.ERPM / 60.0 * physTickSec
		st.Tachometer += int32(tachDelta)
		st.TachometerAbs += int32(math.Abs(tachDelta))

		// Distance from wheel speed
		distDelta := math.Abs(st.Speed) * physTickSec
		st.Distance += distDelta
		st.Odometer += distDelta

		// SAT (Setpoint Adjustment Type)
		st.SAT = SATNone
		if math.Abs(st.DutyCycle) > 0.8 {
			st.SAT = SATPBDuty
		}
		if st.Voltage < st.BatteryVoltageMin+2.0 {
			st.SAT = SATPBLowVoltage
		}

		// Voltage: drain proportional to power draw
		power := math.Abs(st.BattCurrent * st.Voltage)
		// A 720Wh pack at 63V = ~41Ah. V drop = I * R_internal + energy drain
		st.Voltage -= math.Abs(st.BattCurrent) * 0.003 * physTickSec // sag from internal R
		st.Voltage -= power * physTickSec / (41.0 * 3600.0) * st.BatteryVoltageMax // energy drain

		// Counters
		st.AmpHours += math.Abs(st.BattCurrent) * physTickSec / 3600.0
		st.WattHours += power * physTickSec / 3600.0
	}

	// Temperature model: load heats, ambient cools (both states)
	ambientTemp := 25.0
	st.MOSFETTemp += math.Abs(st.MotorCurrent) * 0.0003
	st.MOSFETTemp -= (st.MOSFETTemp - ambientTemp) * 0.0002 // cooling toward ambient
	st.MotorTemp += math.Abs(st.MotorCurrent) * 0.00015
	st.MotorTemp -= (st.MotorTemp - ambientTemp) * 0.00008 // motor cools slower

	// Stats tracking
	absSpeed := math.Abs(st.Speed)
	power := math.Abs(st.BattCurrent * st.Voltage)
	absCurrent := math.Abs(st.MotorCurrent)
	if absSpeed > st.SpeedMax {
		st.SpeedMax = absSpeed
	}
	if power > st.PowerMax {
		st.PowerMax = power
	}
	if absCurrent > st.CurrentMax {
		st.CurrentMax = absCurrent
	}
	if st.MOSFETTemp > st.MOSFETTempMax {
		st.MOSFETTempMax = st.MOSFETTemp
	}
	if st.MotorTemp > st.MotorTempMax {
		st.MotorTempMax = st.MotorTemp
	}
	st.SpeedSum += absSpeed
	st.PowerSum += power
	st.CurrentSum += absCurrent
	st.StatSamples++
}

// BatteryPercent returns 0-1 based on voltage and cell config.
func (s *BoardState) BatteryPercent() float64 {
	if s.BatteryVoltageMax <= s.BatteryVoltageMin {
		return 0.5
	}
	pct := (s.Voltage - s.BatteryVoltageMin) / (s.BatteryVoltageMax - s.BatteryVoltageMin)
	if pct > 1 {
		pct = 1
	}
	if pct < 0 {
		pct = 0
	}
	return pct
}

// appendFloat32Auto encodes using VESC's float32_auto format (5 bytes).
func appendFloat32Auto(buf []byte, v float64) []byte {
	return AppendFloat32Auto(buf, v)
}

func encodeStateCompat(state RunState, stop StopCondition) uint8 {
	switch state {
	case StateStartup:
		return 0
	case StateRunning:
		return 1
	case StateReady:
		switch stop {
		case StopPitch:
			return 6
		case StopRoll:
			return 7
		case StopSwitchHalf:
			return 8
		case StopSwitchFull:
			return 9
		case StopReverseStop:
			return 12
		case StopQuickstop:
			return 13
		default:
			return 11 // FAULT_STARTUP
		}
	case StateDisabled:
		return 15
	default:
		return 11
	}
}

func encodeSATCompat(sat SetpointAdjustmentType) uint8 {
	switch sat {
	case SATCentering:
		return 0
	case SATReverseStop:
		return 1
	case SATNone:
		return 2
	case SATPBDuty:
		return 3
	case SATPBHighVoltage:
		return 4
	case SATPBLowVoltage:
		return 5
	case SATPBTemperature:
		return 6
	case SATPBSpeed:
		return 7
	case SATPBError:
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
