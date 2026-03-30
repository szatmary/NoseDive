package simulator

import (
	"bufio"
	"crypto/sha1"
	"embed"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/szatmary/nosedive/pkg/refloat"
	"github.com/szatmary/nosedive/pkg/vesc"
)

//go:embed web/*
var webFS embed.FS

// StartWeb starts the web GUI server.
func (s *Simulator) StartWeb(addr string) error {
	subFS, err := fs.Sub(webFS, "web")
	if err != nil {
		return fmt.Errorf("embed fs: %w", err)
	}

	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.FS(subFS)))
	mux.HandleFunc("/ws", s.handleWebSocket)

	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return fmt.Errorf("web listen: %w", err)
	}

	s.webAddr = ln.Addr().String()
	go http.Serve(ln, mux)
	return nil
}

// WebAddr returns the web server address.
func (s *Simulator) WebAddr() string {
	return s.webAddr
}

// stateJSON is the JSON representation sent to the web GUI.
type stateJSON struct {
	// VESC
	Voltage float64 `json:"voltage"`
	Fault   string  `json:"fault"`

	// Motor
	ERPM         float64 `json:"erpm"`
	DutyCycle    float64 `json:"dutyCycle"`
	MotorCurrent float64 `json:"motorCurrent"`
	BattCurrent  float64 `json:"battCurrent"`
	MOSFETTemp   float64 `json:"mosfetTemp"`
	MotorTemp    float64 `json:"motorTemp"`
	Speed        float64 `json:"speed"`

	// Refloat state
	RunState      string `json:"runState"`
	Mode          string `json:"mode"`
	SAT           string `json:"sat"`
	StopCondition string `json:"stopCondition"`
	Footpad       string `json:"footpad"`
	Charging      bool   `json:"charging"`

	// IMU
	Pitch float64 `json:"pitch"`
	Roll  float64 `json:"roll"`

	// ADC
	ADC1 float64 `json:"adc1"`
	ADC2 float64 `json:"adc2"`

	// Setpoints
	Setpoint       float64 `json:"setpoint"`
	BalanceCurrent float64 `json:"balanceCurrent"`

	// Counters
	AmpHours    float64 `json:"ampHours"`
	WattHours   float64 `json:"wattHours"`
	Tachometer  int32   `json:"tachometer"`
}

func boardStateToJSON(bs *BoardState) stateJSON {
	return stateJSON{
		Voltage:        bs.Voltage,
		Fault:          bs.Fault.String(),
		ERPM:           bs.ERPM,
		DutyCycle:      bs.DutyCycle,
		MotorCurrent:   bs.MotorCurrent,
		BattCurrent:    bs.BattCurrent,
		MOSFETTemp:     bs.MOSFETTemp,
		MotorTemp:      bs.MotorTemp,
		Speed:          bs.Speed,
		RunState:       bs.RunState.String(),
		Mode:           bs.Mode.String(),
		SAT:            bs.SAT.String(),
		StopCondition:  bs.StopCondition.String(),
		Footpad:        bs.Footpad.String(),
		Charging:       bs.Charging,
		Pitch:          bs.Pitch,
		Roll:           bs.Roll,
		ADC1:           bs.ADC1,
		ADC2:           bs.ADC2,
		Setpoint:       bs.Setpoint,
		BalanceCurrent: bs.BalanceCurrent,
		AmpHours:       bs.AmpHours,
		WattHours:      bs.WattHours,
		Tachometer:     bs.Tachometer,
	}
}

// updateMessage is the JSON received from the web GUI.
type updateMessage struct {
	Field string          `json:"field"`
	Value json.RawMessage `json:"value"`
}

// SetState applies a named field update to the board state.
func (s *Simulator) SetState(field string, value json.RawMessage) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	var f float64
	var str string
	var b bool

	switch field {
	case "voltage":
		if err := json.Unmarshal(value, &f); err != nil {
			return err
		}
		s.state.Voltage = f
	case "mosfetTemp":
		if err := json.Unmarshal(value, &f); err != nil {
			return err
		}
		s.state.MOSFETTemp = f
	case "motorTemp":
		if err := json.Unmarshal(value, &f); err != nil {
			return err
		}
		s.state.MotorTemp = f
	case "pitch":
		if err := json.Unmarshal(value, &f); err != nil {
			return err
		}
		s.state.Pitch = f
	case "roll":
		if err := json.Unmarshal(value, &f); err != nil {
			return err
		}
		s.state.Roll = f
	case "footpad":
		if err := json.Unmarshal(value, &str); err != nil {
			return err
		}
		fp := parseFootpad(str)
		s.state.Footpad = fp
		s.state.ADC1 = 0
		s.state.ADC2 = 0
		if fp == refloat.FootpadLeft || fp == refloat.FootpadBoth {
			s.state.ADC1 = 3.0
		}
		if fp == refloat.FootpadRight || fp == refloat.FootpadBoth {
			s.state.ADC2 = 3.0
		}
	case "runState":
		if err := json.Unmarshal(value, &str); err != nil {
			return err
		}
		s.state.RunState = parseRunState(str)
	case "mode":
		if err := json.Unmarshal(value, &str); err != nil {
			return err
		}
		s.state.Mode = parseMode(str)
	case "fault":
		if err := json.Unmarshal(value, &str); err != nil {
			return err
		}
		s.state.Fault = parseFault(str)
	case "charging":
		if err := json.Unmarshal(value, &b); err != nil {
			return err
		}
		s.state.Charging = b
	default:
		return fmt.Errorf("unknown field: %s", field)
	}
	return nil
}

func parseFootpad(s string) refloat.FootpadState {
	switch strings.ToUpper(s) {
	case "NONE":
		return refloat.FootpadNone
	case "LEFT":
		return refloat.FootpadLeft
	case "RIGHT":
		return refloat.FootpadRight
	case "BOTH":
		return refloat.FootpadBoth
	default:
		return refloat.FootpadNone
	}
}

func parseRunState(s string) refloat.RunState {
	switch strings.ToUpper(s) {
	case "DISABLED":
		return refloat.StateDisabled
	case "STARTUP":
		return refloat.StateStartup
	case "READY":
		return refloat.StateReady
	case "RUNNING":
		return refloat.StateRunning
	default:
		return refloat.StateReady
	}
}

func parseMode(s string) refloat.Mode {
	switch strings.ToUpper(s) {
	case "NORMAL":
		return refloat.ModeNormal
	case "HANDTEST":
		return refloat.ModeHandtest
	case "FLYWHEEL":
		return refloat.ModeFlywheel
	default:
		return refloat.ModeNormal
	}
}

func parseFault(s string) vesc.FaultCode {
	switch strings.ToUpper(s) {
	case "NONE":
		return vesc.FaultNone
	case "OVER_VOLTAGE":
		return vesc.FaultOverVoltage
	case "UNDER_VOLTAGE":
		return vesc.FaultUnderVoltage
	case "DRV":
		return vesc.FaultDRV
	case "ABS_OVER_CURRENT":
		return vesc.FaultAbsOverCurrent
	case "OVER_TEMP_FET":
		return vesc.FaultOverTempFET
	case "OVER_TEMP_MOTOR":
		return vesc.FaultOverTempMotor
	default:
		return vesc.FaultNone
	}
}

// handleWebSocket upgrades an HTTP connection to WebSocket (RFC 6455).
func (s *Simulator) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := upgradeWebSocket(w, r)
	if err != nil {
		log.Printf("web: websocket upgrade failed: %v", err)
		return
	}
	defer conn.Close()

	var writeMu sync.Mutex
	done := make(chan struct{})

	// Push state at 10Hz
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-done:
				return
			case <-ticker.C:
				st := s.State()
				data, _ := json.Marshal(boardStateToJSON(&st))
				writeMu.Lock()
				err := wsWriteText(conn, data)
				writeMu.Unlock()
				if err != nil {
					return
				}
			}
		}
	}()

	// Read updates from client
	for {
		msg, err := wsReadMessage(conn)
		if err != nil {
			break
		}
		var update updateMessage
		if err := json.Unmarshal(msg, &update); err != nil {
			continue
		}
		if err := s.SetState(update.Field, update.Value); err != nil {
			log.Printf("web: set %s: %v", update.Field, err)
		}
	}
	close(done)
}

// Minimal WebSocket implementation (RFC 6455) using net/http hijacking.

const wsGUID = "258EAFA5-E914-47DA-95CA-5AB5DC085B11"

func upgradeWebSocket(w http.ResponseWriter, r *http.Request) (net.Conn, error) {
	if !strings.EqualFold(r.Header.Get("Upgrade"), "websocket") {
		http.Error(w, "not a websocket request", http.StatusBadRequest)
		return nil, fmt.Errorf("not a websocket request")
	}

	key := r.Header.Get("Sec-WebSocket-Key")
	if key == "" {
		http.Error(w, "missing key", http.StatusBadRequest)
		return nil, fmt.Errorf("missing Sec-WebSocket-Key")
	}

	h := sha1.New()
	h.Write([]byte(key + wsGUID))
	acceptKey := base64.StdEncoding.EncodeToString(h.Sum(nil))

	hj, ok := w.(http.Hijacker)
	if !ok {
		http.Error(w, "hijack not supported", http.StatusInternalServerError)
		return nil, fmt.Errorf("hijack not supported")
	}

	conn, bufrw, err := hj.Hijack()
	if err != nil {
		return nil, err
	}

	resp := "HTTP/1.1 101 Switching Protocols\r\n" +
		"Upgrade: websocket\r\n" +
		"Connection: Upgrade\r\n" +
		"Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n"
	bufrw.WriteString(resp)
	bufrw.Flush()

	return conn, nil
}

func wsWriteText(conn net.Conn, data []byte) error {
	return wsWriteFrame(conn, 1, data) // opcode 1 = text
}

func wsWriteFrame(conn net.Conn, opcode byte, data []byte) error {
	n := len(data)
	var header []byte

	header = append(header, 0x80|opcode) // FIN + opcode
	if n < 126 {
		header = append(header, byte(n))
	} else if n < 65536 {
		header = append(header, 126)
		header = append(header, byte(n>>8), byte(n))
	} else {
		header = append(header, 127)
		b := make([]byte, 8)
		binary.BigEndian.PutUint64(b, uint64(n))
		header = append(header, b...)
	}

	conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
	if _, err := conn.Write(header); err != nil {
		return err
	}
	if _, err := conn.Write(data); err != nil {
		return err
	}
	return nil
}

func wsReadMessage(conn net.Conn) ([]byte, error) {
	r := bufio.NewReader(conn)
	var result []byte

	for {
		conn.SetReadDeadline(time.Now().Add(60 * time.Second))

		// Read first 2 bytes
		b0, err := r.ReadByte()
		if err != nil {
			return nil, err
		}
		b1, err := r.ReadByte()
		if err != nil {
			return nil, err
		}

		fin := b0&0x80 != 0
		opcode := b0 & 0x0F
		masked := b1&0x80 != 0
		payLen := uint64(b1 & 0x7F)

		// Close frame
		if opcode == 8 {
			return nil, io.EOF
		}
		// Ping → pong
		if opcode == 9 {
			pongData := make([]byte, payLen)
			io.ReadFull(r, pongData)
			wsWriteFrame(conn, 10, pongData) // opcode 10 = pong
			continue
		}

		if payLen == 126 {
			var buf [2]byte
			if _, err := io.ReadFull(r, buf[:]); err != nil {
				return nil, err
			}
			payLen = uint64(binary.BigEndian.Uint16(buf[:]))
		} else if payLen == 127 {
			var buf [8]byte
			if _, err := io.ReadFull(r, buf[:]); err != nil {
				return nil, err
			}
			payLen = binary.BigEndian.Uint64(buf[:])
		}

		var mask [4]byte
		if masked {
			if _, err := io.ReadFull(r, mask[:]); err != nil {
				return nil, err
			}
		}

		payload := make([]byte, payLen)
		if _, err := io.ReadFull(r, payload); err != nil {
			return nil, err
		}

		if masked {
			for i := range payload {
				payload[i] ^= mask[i%4]
			}
		}

		result = append(result, payload...)
		if fin {
			break
		}
	}

	return result, nil
}

