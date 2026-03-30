package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/szatmary/nosedive/pkg/refloat"
	"github.com/szatmary/nosedive/pkg/simulator"
	"github.com/szatmary/nosedive/pkg/vesc"
)

func main() {
	addr := flag.String("addr", "", "VESC TCP address (host:port)")
	sim := flag.Bool("sim", false, "Start built-in simulator")
	simAddr := flag.String("sim-addr", "127.0.0.1:0", "Simulator listen address")
	flag.Parse()

	var conn *vesc.Connection
	var simInstance *simulator.Simulator

	if *sim {
		simInstance = simulator.New()
		if err := simInstance.Start(*simAddr); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to start simulator: %v\n", err)
			os.Exit(1)
		}
		defer simInstance.Stop()
		actualAddr := simInstance.Addr()
		fmt.Printf("Simulator listening on %s\n", actualAddr)

		var err error
		conn, err = vesc.DialTCP(actualAddr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to connect to simulator: %v\n", err)
			os.Exit(1)
		}
	} else if *addr != "" {
		var err error
		conn, err = vesc.DialTCP(*addr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to connect: %v\n", err)
			os.Exit(1)
		}
	} else {
		fmt.Println("NoseDive - VESC Refloat CLI")
		fmt.Println()
		fmt.Println("Usage:")
		fmt.Println("  nosedive --sim              Start with built-in simulator")
		fmt.Println("  nosedive --addr host:port   Connect to VESC over TCP")
		fmt.Println()
		flag.PrintDefaults()
		os.Exit(0)
	}
	defer conn.Close()

	client := refloat.NewClient(conn)
	cli := &CLI{
		client: client,
		sim:    simInstance,
	}
	cli.Run()
}

// CLI handles the interactive command loop.
type CLI struct {
	client *refloat.Client
	sim    *simulator.Simulator
}

func (c *CLI) Run() {
	fmt.Println("NoseDive CLI - Type 'help' for commands")
	fmt.Println()

	// Print initial connection info
	c.cmdFW()

	scanner := bufio.NewScanner(os.Stdin)
	for {
		fmt.Print("nosedive> ")
		if !scanner.Scan() {
			break
		}
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		parts := strings.Fields(line)
		cmd := strings.ToLower(parts[0])
		args := parts[1:]

		switch cmd {
		case "help", "h", "?":
			c.cmdHelp()
		case "fw", "firmware":
			c.cmdFW()
		case "info", "i":
			c.cmdInfo()
		case "values", "v":
			c.cmdValues()
		case "rt", "rtdata":
			c.cmdRTData()
		case "all", "alldata":
			mode := uint8(2)
			if len(args) > 0 {
				if m, err := strconv.Atoi(args[0]); err == nil {
					mode = uint8(m)
				}
			}
			c.cmdAllData(mode)
		case "watch", "w":
			interval := 500 * time.Millisecond
			if len(args) > 0 {
				if d, err := time.ParseDuration(args[0]); err == nil {
					interval = d
				}
			}
			c.cmdWatch(interval)
		case "alive":
			c.cmdAlive()

		// Simulator-only commands
		case "footpad", "fp":
			c.cmdSimFootpad(args)
		case "pitch":
			c.cmdSimPitch(args)
		case "roll":
			c.cmdSimRoll(args)
		case "state", "st":
			c.cmdSimState()

		case "quit", "exit", "q":
			fmt.Println("Goodbye!")
			return
		default:
			fmt.Printf("Unknown command: %s (type 'help' for commands)\n", cmd)
		}
	}
}

func (c *CLI) cmdHelp() {
	fmt.Println(`Commands:
  VESC / Refloat:
    fw              Show VESC firmware version
    info            Show Refloat package info
    values          Show VESC values (voltage, current, temp, etc.)
    rt              Show Refloat real-time data
    all [mode]      Show Refloat compact data (mode 0-4, default 2)
    watch [dur]     Live-stream RT data (default 500ms interval, Ctrl+C to stop)
    alive           Send keepalive

  Simulator (only with --sim):
    footpad <state> Set footpad: none, left, right, both
    pitch <deg>     Set board pitch in degrees
    roll <deg>      Set board roll in degrees
    state           Show simulator internal state

  General:
    help            Show this help
    quit            Exit`)
}

func (c *CLI) cmdFW() {
	fw, err := c.client.GetFWVersion()
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	fmt.Printf("VESC FW: %d.%d  HW: %s\n", fw.Major, fw.Minor, fw.HWName)
}

func (c *CLI) cmdInfo() {
	info, err := c.client.GetInfo()
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	fmt.Printf("Package: %s v%d.%d.%d%s\n", info.Name, info.MajorVersion, info.MinorVersion, info.PatchVersion, dashIfNonEmpty(info.Suffix))
}

func (c *CLI) cmdValues() {
	v, err := c.client.GetVESCValues()
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	fmt.Printf("  Voltage:       %.1f V\n", v.Voltage)
	fmt.Printf("  Motor Current: %.2f A\n", v.AvgMotorCurrent)
	fmt.Printf("  Input Current: %.2f A\n", v.AvgInputCurrent)
	fmt.Printf("  Duty Cycle:    %.1f%%\n", v.DutyCycle*100)
	fmt.Printf("  RPM:           %.0f\n", v.RPM)
	fmt.Printf("  MOSFET Temp:   %.1f C\n", v.TempMOSFET)
	fmt.Printf("  Motor Temp:    %.1f C\n", v.TempMotor)
	fmt.Printf("  Ah:            %.4f\n", v.AmpHours)
	fmt.Printf("  Wh:            %.4f\n", v.WattHours)
	fmt.Printf("  Fault:         %s\n", v.Fault)
}

func (c *CLI) cmdRTData() {
	rt, err := c.client.GetRTData()
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	printRTData(rt)
}

func (c *CLI) cmdAllData(mode uint8) {
	rt, err := c.client.GetAllData(mode)
	if err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	printRTData(rt)
}

func (c *CLI) cmdWatch(interval time.Duration) {
	fmt.Printf("Streaming RT data every %s (press Enter to stop)...\n", interval)

	stopCh := make(chan struct{})
	go func() {
		buf := make([]byte, 1)
		os.Stdin.Read(buf)
		close(stopCh)
	}()

	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for {
		select {
		case <-stopCh:
			fmt.Println("Stopped.")
			return
		case <-ticker.C:
			rt, err := c.client.GetAllData(2)
			if err != nil {
				fmt.Printf("Error: %v\n", err)
				return
			}
			// Clear screen and reprint
			fmt.Print("\033[2J\033[H")
			fmt.Println("NoseDive Live View (press Enter to stop)")
			fmt.Println(strings.Repeat("-", 50))
			printRTData(rt)
		}
	}
}

func (c *CLI) cmdAlive() {
	if err := c.client.SendAlive(); err != nil {
		fmt.Printf("Error: %v\n", err)
		return
	}
	fmt.Println("Keepalive sent.")
}

func (c *CLI) cmdSimFootpad(args []string) {
	if c.sim == nil {
		fmt.Println("Simulator not running. Use --sim flag.")
		return
	}
	if len(args) == 0 {
		fmt.Println("Usage: footpad <none|left|right|both>")
		return
	}
	var state refloat.FootpadState
	switch strings.ToLower(args[0]) {
	case "none", "off", "0":
		state = refloat.FootpadNone
	case "left", "l", "1":
		state = refloat.FootpadLeft
	case "right", "r", "2":
		state = refloat.FootpadRight
	case "both", "b", "3":
		state = refloat.FootpadBoth
	default:
		fmt.Printf("Unknown footpad state: %s\n", args[0])
		return
	}
	c.sim.SetFootpad(state)
	fmt.Printf("Footpad set to %s\n", state)
}

func (c *CLI) cmdSimPitch(args []string) {
	if c.sim == nil {
		fmt.Println("Simulator not running. Use --sim flag.")
		return
	}
	if len(args) == 0 {
		fmt.Println("Usage: pitch <degrees>")
		return
	}
	deg, err := strconv.ParseFloat(args[0], 64)
	if err != nil {
		fmt.Printf("Invalid number: %s\n", args[0])
		return
	}
	c.sim.SetPitch(deg)
	fmt.Printf("Pitch set to %.1f degrees\n", deg)
}

func (c *CLI) cmdSimRoll(args []string) {
	if c.sim == nil {
		fmt.Println("Simulator not running. Use --sim flag.")
		return
	}
	if len(args) == 0 {
		fmt.Println("Usage: roll <degrees>")
		return
	}
	deg, err := strconv.ParseFloat(args[0], 64)
	if err != nil {
		fmt.Printf("Invalid number: %s\n", args[0])
		return
	}
	c.sim.SetRoll(deg)
	fmt.Printf("Roll set to %.1f degrees\n", deg)
}

func (c *CLI) cmdSimState() {
	if c.sim == nil {
		fmt.Println("Simulator not running. Use --sim flag.")
		return
	}
	st := c.sim.State()
	fmt.Printf("  Run State:    %s\n", st.RunState)
	fmt.Printf("  Mode:         %s\n", st.Mode)
	fmt.Printf("  Stop:         %s\n", st.StopCondition)
	fmt.Printf("  Footpad:      %s\n", st.Footpad)
	fmt.Printf("  Pitch:        %.1f deg\n", st.Pitch)
	fmt.Printf("  Roll:         %.1f deg\n", st.Roll)
	fmt.Printf("  Speed:        %.2f m/s\n", st.Speed)
	fmt.Printf("  ERPM:         %.0f\n", st.ERPM)
	fmt.Printf("  Voltage:      %.1f V\n", st.Voltage)
	fmt.Printf("  Motor Temp:   %.1f C\n", st.MotorTemp)
	fmt.Printf("  MOSFET Temp:  %.1f C\n", st.MOSFETTemp)
}

func printRTData(rt *refloat.RTData) {
	fmt.Printf("  State:         %s\n", rt.State.RunState)
	if rt.State.StopCondition != refloat.StopNone {
		fmt.Printf("  Stop:          %s\n", rt.State.StopCondition)
	}
	if rt.State.SAT != refloat.SATNone {
		fmt.Printf("  SAT:           %s\n", rt.State.SAT)
	}
	fmt.Printf("  Footpad:       %s\n", rt.State.Footpad)
	fmt.Printf("  Pitch:         %.1f deg\n", rt.Pitch)
	fmt.Printf("  Roll:          %.1f deg\n", rt.Roll)
	fmt.Printf("  Speed:         %.2f m/s (%.1f km/h)\n", rt.Speed, rt.Speed*3.6)
	fmt.Printf("  ERPM:          %.0f\n", rt.ERPM)
	fmt.Printf("  Duty:          %.1f%%\n", rt.DutyCycle*100)
	fmt.Printf("  Motor Current: %.2f A\n", rt.MotorCurrent)
	fmt.Printf("  Batt Voltage:  %.1f V\n", rt.BattVoltage)
	fmt.Printf("  Batt Current:  %.2f A\n", rt.BattCurrent)
	fmt.Printf("  MOSFET Temp:   %.1f C\n", rt.MOSFETTemp)
	fmt.Printf("  Motor Temp:    %.1f C\n", rt.MotorTemp)
	fmt.Printf("  ADC1/ADC2:     %.2f / %.2f\n", rt.ADC1, rt.ADC2)
}

func dashIfNonEmpty(s string) string {
	if s == "" {
		return ""
	}
	return "-" + s
}
