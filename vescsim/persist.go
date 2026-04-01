package main

import (
	"encoding/json"
	"log"
	"os"
	"path/filepath"
)

// savedState is the JSON structure persisted to disk.
type savedState struct {
	MCConf  *MCConfiguration  `json:"mc_conf,omitempty"`
	AppConf *AppConfiguration `json:"app_conf,omitempty"`
	// Raw configs for types we don't have full struct support for
	CustomConfigData []byte `json:"custom_config_data,omitempty"`
}

// SaveState writes the current configs to a JSON file.
// Caller must NOT hold s.mu.
func (s *Simulator) SaveState(path string) error {
	s.mu.Lock()
	state := &savedState{
		CustomConfigData: s.state.ConfigData,
	}
	if mcconf, ok := DeserializeMCConf(s.state.MCConf); ok {
		state.MCConf = mcconf
	}
	if appconf, ok := DeserializeAppConf(s.state.AppConf); ok {
		state.AppConf = appconf
	}
	s.mu.Unlock()

	data, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return err
	}

	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return err
	}
	return os.WriteFile(path, data, 0644)
}

// persistState saves state if a statePath is configured. Called with lock held.
func (s *Simulator) persistState() {
	path := s.statePath // snapshot while lock is held by caller
	if path == "" {
		return
	}
	// Save in background to avoid blocking command handling
	go func() {
		if err := s.SaveState(path); err != nil {
			log.Printf("persist: save error: %v", err)
		}
	}()
}

// LoadState reads saved configs from a JSON file and applies them.
func (s *Simulator) LoadState(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}

	var state savedState
	if err := json.Unmarshal(data, &state); err != nil {
		return err
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	if state.MCConf != nil {
		s.state.MCConf = SerializeMCConf(state.MCConf)
		log.Printf("persist: loaded MCConf (cells=%d wheel=%.0fmm R=%.4fΩ)",
			state.MCConf.SiBatteryCells, state.MCConf.SiWheelDiameter*1e3, state.MCConf.FocMotorR)

		// Update simulator voltage from cell count
		if state.MCConf.SiBatteryCells > 0 {
			s.state.Voltage = float64(state.MCConf.SiBatteryCells) * 4.15 // ~full charge
		}
	}
	if state.AppConf != nil {
		s.state.AppConf = SerializeAppConf(state.AppConf)
		log.Printf("persist: loaded AppConf (controller_id=%d)", state.AppConf.ControllerId)
	}
	if state.CustomConfigData != nil {
		s.state.ConfigData = state.CustomConfigData
		log.Printf("persist: loaded custom config (%d bytes)", len(state.CustomConfigData))
	}

	return nil
}
