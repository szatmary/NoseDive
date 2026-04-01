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

func TestCANBusPing(t *testing.T) {
	sim := New()
	if err := sim.Start("127.0.0.1:0"); err != nil {
		t.Fatal(err)
	}
	defer sim.Stop()

	// PING_CAN should return controller ID 0 (main VESC) + 253 (Express) + 10 (BMS)
	resp := sim.HandleCommand([]byte{byte(vesc.CommPingCAN)})
	if resp == nil {
		t.Fatal("PingCAN returned nil")
	}
	if len(resp) != 4 {
		t.Fatalf("PingCAN response length = %d, want 4 (cmd + 3 devices)", len(resp))
	}
	if resp[0] != byte(vesc.CommPingCAN) {
		t.Errorf("PingCAN cmd = 0x%02x, want 0x%02x", resp[0], byte(vesc.CommPingCAN))
	}
	if resp[1] != 0 {
		t.Errorf("PingCAN main ID = %d, want 0", resp[1])
	}
	if resp[2] != 253 {
		t.Errorf("PingCAN express ID = %d, want 253", resp[2])
	}
	if resp[3] != 10 {
		t.Errorf("PingCAN BMS ID = %d, want 10", resp[3])
	}
}

func TestCANForwardToExpress(t *testing.T) {
	sim := New()
	if err := sim.Start("127.0.0.1:0"); err != nil {
		t.Fatal(err)
	}
	defer sim.Stop()

	// Forward FW_VERSION to controller 253 (VESC Express)
	payload := []byte{byte(vesc.CommForwardCAN), 253, byte(vesc.CommFWVersion)}
	resp := sim.HandleCommand(payload)
	if resp == nil {
		t.Fatal("ForwardCAN to Express returned nil")
	}
	if resp[0] != byte(vesc.CommFWVersion) {
		t.Fatalf("response cmd = 0x%02x, want CommFWVersion", resp[0])
	}
	// Check HW type byte — should be VESCExpress (3)
	// Format: [cmd][major][minor][hw_name\0][uuid:12][paired:1][test:1][hw_type:1]...
	// Find hw_type: skip cmd(1) + major(1) + minor(1) + hwname + null + uuid(12) + paired(1) + test(1)
	idx := 3
	for idx < len(resp) && resp[idx] != 0 {
		idx++
	}
	idx++ // skip null terminator
	idx += 12 // uuid
	idx += 1  // isPaired
	idx += 1  // fw test version
	if idx >= len(resp) {
		t.Fatal("response too short to contain hw_type")
	}
	hwType := vesc.HWType(resp[idx])
	if hwType != vesc.HWTypeVESCExpress {
		t.Errorf("HW type = %d, want %d (VESCExpress)", hwType, vesc.HWTypeVESCExpress)
	}
}

func TestCANForwardToSelf(t *testing.T) {
	sim := New()
	if err := sim.Start("127.0.0.1:0"); err != nil {
		t.Fatal(err)
	}
	defer sim.Stop()

	// Forward FW_VERSION to controller 0 (self)
	payload := []byte{byte(vesc.CommForwardCAN), 0, byte(vesc.CommFWVersion)}
	resp := sim.HandleCommand(payload)
	if resp == nil {
		t.Fatal("ForwardCAN to self returned nil")
	}
	// HW type should be HWTypeVESC (0) — the main motor controller
	idx := 3
	for idx < len(resp) && resp[idx] != 0 {
		idx++
	}
	idx++ // null
	idx += 12 // uuid
	idx += 1  // isPaired
	idx += 1  // fw test version
	if idx >= len(resp) {
		t.Fatal("response too short")
	}
	hwType := vesc.HWType(resp[idx])
	if hwType != vesc.HWTypeVESC {
		t.Errorf("HW type = %d, want %d (VESC)", hwType, vesc.HWTypeVESC)
	}
}

func TestCANForwardUnknownDevice(t *testing.T) {
	sim := New()

	// Forward to non-existent controller 99
	payload := []byte{byte(vesc.CommForwardCAN), 99, byte(vesc.CommFWVersion)}
	resp := sim.HandleCommand(payload)
	if resp != nil {
		t.Errorf("ForwardCAN to unknown device should return nil, got %d bytes", len(resp))
	}
}

func TestRefloatInstallFlow(t *testing.T) {
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

	// Verify Refloat is installed by default
	info, err := client.GetInfo()
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "Refloat" {
		t.Fatalf("expected Refloat installed, got %q", info.Name)
	}

	// Disable Refloat
	sim.SetHasRefloat(false)

	// FW version should now report customConfigCount=0
	fw, err := client.GetFWVersion()
	if err != nil {
		t.Fatal(err)
	}
	if fw.CustomConfigNum != 0 {
		t.Errorf("customConfigNum = %d after disabling Refloat, want 0", fw.CustomConfigNum)
	}

	// Refloat info query should return nil (no response)
	resp := sim.HandleCommand([]byte{byte(vesc.CommCustomAppData), refloat.PackageMagic, byte(refloat.CommandInfo)})
	if resp != nil {
		t.Errorf("Refloat info should return nil when not installed, got %d bytes", len(resp))
	}

	// Simulate installing Refloat via COMM_ERASE_NEW_APP + COMM_WRITE_NEW_APP_DATA
	eraseResp := sim.HandleCommand([]byte{byte(vesc.CommEraseNewApp)})
	if eraseResp == nil || eraseResp[0] != byte(vesc.CommEraseNewApp) {
		t.Fatal("COMM_ERASE_NEW_APP should return ack")
	}

	writeResp := sim.HandleCommand([]byte{byte(vesc.CommWriteNewAppData), 0x00}) // dummy data
	if writeResp == nil || writeResp[0] != byte(vesc.CommWriteNewAppData) {
		t.Fatal("COMM_WRITE_NEW_APP_DATA should return ack")
	}

	// Now FW version should report customConfigCount=1
	fw, err = client.GetFWVersion()
	if err != nil {
		t.Fatal(err)
	}
	if fw.CustomConfigNum != 1 {
		t.Errorf("customConfigNum = %d after installing Refloat, want 1", fw.CustomConfigNum)
	}

	// Refloat info should work again
	info, err = client.GetInfo()
	if err != nil {
		t.Fatal(err)
	}
	if info.Name != "Refloat" {
		t.Errorf("package name = %q after install, want Refloat", info.Name)
	}
}
