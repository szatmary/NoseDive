package board

import (
	"encoding/json"
	"fmt"
	"os"
)

// Profile describes a complete onewheel board configuration.
type Profile struct {
	Name         string `json:"name"`
	Manufacturer string `json:"manufacturer"`
	Model        string `json:"model"`
	Description  string `json:"description"`

	Controller Controller  `json:"controller"`
	Motor      Motor       `json:"motor"`
	Battery    Battery     `json:"battery"`
	Wheel      Wheel       `json:"wheel"`
	Performance Performance `json:"performance"`
}

// Controller describes the VESC hardware.
type Controller struct {
	Type     string   `json:"type"`
	Hardware string   `json:"hardware"`
	Firmware Firmware `json:"firmware"`

	MaxCurrent      float64 `json:"maxCurrent"`
	MaxBrakeCurrent float64 `json:"maxBrakeCurrent"`
}

// Firmware describes the VESC firmware version.
type Firmware struct {
	Major uint8 `json:"major"`
	Minor uint8 `json:"minor"`
}

// Motor describes the hub motor.
type Motor struct {
	Type  string `json:"type"`
	Name  string `json:"name"`
	Notes string `json:"notes"`

	PolePairs      int     `json:"polePairs"`
	Resistance     float64 `json:"resistance"`     // ohms
	Inductance     float64 `json:"inductance"`     // henries
	FluxLinkage    float64 `json:"fluxLinkage"`    // weber
	MaxCurrent     float64 `json:"maxCurrent"`     // amps
	MaxBrakeCurrent float64 `json:"maxBrakeCurrent"` // amps (negative)
	KV             float64 `json:"kv"`             // RPM/V

	HallSensorTable []int `json:"hallSensorTable"` // 8 entries
}

// Battery describes the battery pack.
type Battery struct {
	Chemistry  string `json:"chemistry"`
	CellType   string `json:"cellType"`
	Config     string `json:"configuration"`

	SeriesCells   int `json:"seriesCells"`
	ParallelCells int `json:"parallelCells"`

	CapacityAh float64 `json:"capacityAh"`
	CapacityWh float64 `json:"capacityWh"`

	VoltageMin     float64 `json:"voltageMin"`
	VoltageNominal float64 `json:"voltageNominal"`
	VoltageMax     float64 `json:"voltageMax"`
	CutoffStart    float64 `json:"cutoffStart"`
	CutoffEnd      float64 `json:"cutoffEnd"`

	MaxDischargeCurrent float64 `json:"maxDischargeCurrent"`
	MaxChargeCurrent    float64 `json:"maxChargeCurrent"`

	CellMinVoltage     float64 `json:"cellMinVoltage"`
	CellMaxVoltage     float64 `json:"cellMaxVoltage"`
	CellNominalVoltage float64 `json:"cellNominalVoltage"`
}

// Wheel describes the tire/wheel.
type Wheel struct {
	Diameter       float64 `json:"diameter"`
	DiameterUnit   string  `json:"diameterUnit"`
	TirePressurePSI float64 `json:"tirePressurePSI"`
	CircumferenceM float64 `json:"circumferenceM"`
}

// Performance holds observed performance specs.
type Performance struct {
	TopSpeedMPH float64 `json:"topSpeedMPH"`
	RangeMiles  float64 `json:"rangeMiles"`
	WeightLbs   float64 `json:"weightLbs"`
}

// LoadProfile reads a board profile from a JSON file.
func LoadProfile(path string) (*Profile, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read profile: %w", err)
	}
	var p Profile
	if err := json.Unmarshal(data, &p); err != nil {
		return nil, fmt.Errorf("parse profile: %w", err)
	}
	return &p, nil
}

// ERPMPerMPS returns the ERPM per meter/second based on motor pole pairs
// and wheel circumference. Useful for converting between speed and ERPM.
func (p *Profile) ERPMPerMPS() float64 {
	if p.Wheel.CircumferenceM == 0 {
		return 0
	}
	// ERPM = mechanical_RPM * pole_pairs
	// mechanical_RPM = (speed_m/s / circumference_m) * 60
	return float64(p.Motor.PolePairs) * 60.0 / p.Wheel.CircumferenceM
}

// SpeedFromERPM converts ERPM to m/s.
func (p *Profile) SpeedFromERPM(erpm float64) float64 {
	epmps := p.ERPMPerMPS()
	if epmps == 0 {
		return 0
	}
	return erpm / epmps
}

// ERPMFromSpeed converts m/s to ERPM.
func (p *Profile) ERPMFromSpeed(mps float64) float64 {
	return mps * p.ERPMPerMPS()
}

// BatteryPercentage estimates SOC from voltage (simple linear approximation).
func (p *Profile) BatteryPercentage(voltage float64) float64 {
	if voltage >= p.Battery.VoltageMax {
		return 100.0
	}
	if voltage <= p.Battery.VoltageMin {
		return 0.0
	}
	return (voltage - p.Battery.VoltageMin) / (p.Battery.VoltageMax - p.Battery.VoltageMin) * 100.0
}
