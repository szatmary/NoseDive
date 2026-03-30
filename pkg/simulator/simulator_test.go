package simulator

import (
	"testing"
	"time"

	"github.com/szatmary/nosedive/pkg/refloat"
	"github.com/szatmary/nosedive/pkg/vesc"
)

func TestSimulatorEndToEnd(t *testing.T) {
	sim := New()
	if err := sim.Start("127.0.0.1:0"); err != nil {
		t.Fatal(err)
	}
	defer sim.Stop()

	conn, err := vesc.DialTCP(sim.Addr())
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()

	client := refloat.NewClient(conn)

	// Test FW version
	fw, err := client.GetFWVersion()
	if err != nil {
		t.Fatal(err)
	}
	if fw.Major != 6 || fw.Minor != 5 {
		t.Errorf("FW version = %d.%d, want 6.5", fw.Major, fw.Minor)
	}

	// Test VESC values
	vals, err := client.GetVESCValues()
	if err != nil {
		t.Fatal(err)
	}
	if vals.Voltage < 60 || vals.Voltage > 70 {
		t.Errorf("Voltage = %.1f, want ~63", vals.Voltage)
	}

	// Test Refloat info
	info, err := client.GetInfo()
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "Refloat" {
		t.Errorf("Package name = %q, want %q", info.Name, "Refloat")
	}

	// Test RT data while idle
	rt, err := client.GetAllData(2)
	if err != nil {
		t.Fatal(err)
	}
	if rt.State.RunState != refloat.StateReady {
		t.Errorf("Run state = %s, want READY", rt.State.RunState)
	}

	// Simulate stepping on board
	sim.SetFootpad(refloat.FootpadBoth)
	time.Sleep(50 * time.Millisecond) // wait for physics tick

	for range 10 {
		rt, err = client.GetAllData(2)
		if err != nil {
			t.Fatal(err)
		}
		if rt.State.RunState == refloat.StateRunning {
			break
		}
	}
	if rt.State.RunState != refloat.StateRunning {
		t.Errorf("Run state after footpad = %s, want RUNNING", rt.State.RunState)
	}

	// Simulate stepping off
	sim.SetFootpad(refloat.FootpadNone)
	time.Sleep(50 * time.Millisecond) // wait for physics tick
	for range 10 {
		rt, err = client.GetAllData(2)
		if err != nil {
			t.Fatal(err)
		}
		if rt.State.RunState == refloat.StateReady {
			break
		}
	}
	if rt.State.RunState != refloat.StateReady {
		t.Errorf("Run state after step off = %s, want READY", rt.State.RunState)
	}
	if rt.State.StopCondition != refloat.StopSwitchFull {
		t.Errorf("Stop condition = %s, want SWITCH_FULL", rt.State.StopCondition)
	}
}
