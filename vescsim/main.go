package main

import (
	"crypto/rand"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"
)

func main() {
	listenAddr := flag.String("addr", "127.0.0.1:0", "TCP listen address")
	ble := flag.Bool("ble", false, "Enable BLE advertising (macOS only)")
	bleName := flag.String("ble-name", "VESC SIM", "BLE device name")
	web := flag.Bool("web", false, "Enable web GUI")
	webAddr := flag.String("web-addr", "127.0.0.1:8080", "Web GUI listen address")
	profilePath := flag.String("profile", "", "Board profile JSON file")
	dataPath := flag.String("data", "vescsim_state.json", "Path to save/load simulator state")
	fresh := flag.Bool("fresh", false, "Start as unconfigured board (new UUID, default configs, no saved state)")
	noRefloat := flag.Bool("no-refloat", false, "Start without Refloat installed")
	flag.Parse()

	var sim *Simulator
	if *profilePath != "" {
		p, err := LoadProfile(*profilePath)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to load profile: %v\n", err)
			os.Exit(1)
		}
		sim = NewWithProfile(p)
		fmt.Printf("Loaded profile: %s\n", p.Name)
	} else {
		sim = New()
	}

	if *fresh {
		// Fresh board: random UUID, default configs, idle state, delete saved state
		var uuid [12]byte
		if _, err := rand.Read(uuid[:]); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to generate UUID: %v\n", err)
			os.Exit(1)
		}
		sim.SetUUID(uuid)
		sim.SetFootpad(FootpadNone)
		sim.SetRunState(StateReady)
		sim.SetHasRefloat(false)
		os.Remove(*dataPath)
		fmt.Printf("Fresh board mode — UUID: %x\n", uuid)
	}

	if *noRefloat {
		sim.SetHasRefloat(false)
	}

	if !*fresh {
		// Load saved state if it exists
		if err := sim.LoadState(*dataPath); err == nil {
			fmt.Printf("Loaded saved state from %s\n", *dataPath)
		}
	}

	sim.statePath = *dataPath

	if err := sim.Start(*listenAddr); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to start simulator: %v\n", err)
		os.Exit(1)
	}
	defer sim.Stop()
	fmt.Printf("Simulator listening on %s\n", sim.Addr())

	if *ble {
		if err := sim.StartBLE(*bleName); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to start BLE: %v\n", err)
			fmt.Println("(continuing with TCP only)")
		} else {
			fmt.Printf("BLE advertising as %q\n", *bleName)
		}
	}

	if *web {
		if err := sim.StartWeb(*webAddr); err != nil {
			fmt.Fprintf(os.Stderr, "Failed to start web GUI: %v\n", err)
			fmt.Println("(continuing without web GUI)")
		} else {
			fmt.Printf("Web GUI at http://%s\n", sim.WebAddr())
		}
	}

	fmt.Println("VESC Simulator running. Press Ctrl+C to stop.")
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	fmt.Println("\nShutting down.")
}
