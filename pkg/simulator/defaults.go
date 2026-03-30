package simulator

import (
	_ "embed"
	"encoding/binary"
	"encoding/xml"
	"log"
	"math"
	"strconv"
)

//go:embed refloat_settings.xml
var refloatConfigXML []byte

// configParam represents a single parameter from the VESC config XML.
type configParam struct {
	XMLName       xml.Name
	LongName      string `xml:"longName"`
	Type          int    `xml:"type"`
	Transmittable *int   `xml:"transmittable"`
	ValDouble     string `xml:"valDouble"`
	ValInt        string `xml:"valInt"`
	ValString     string `xml:"valString"`
	ValBool       string `xml:"valBool"`
	ValEnum       string `xml:"valEnum"`
}

// configParams is the top-level XML structure.
type configParams struct {
	Params []configParam `xml:"Params>*"`
}

// generateDefaultConfig parses the Refloat settings XML and serializes
// default values in the same order/format as VESC's confparser.
//
// VESC config types:
//   - 0 = int    → int32 big-endian
//   - 1 = double → float32_auto (IEEE754 big-endian)
//   - 2 = enum   → int32 big-endian
//   - 3 = string → skipped (not transmitted)
//   - 4 = bool   → uint8 (0 or 1)
func generateDefaultConfig(xmlData []byte) []byte {
	var cfg configParams
	if err := xml.Unmarshal(xmlData, &cfg); err != nil {
		log.Printf("simulator: failed to parse config XML: %v", err)
		return nil
	}

	var buf []byte
	for _, p := range cfg.Params {
		// Skip non-transmittable params (transmittable defaults to 1 if absent)
		if p.Transmittable != nil && *p.Transmittable == 0 {
			continue
		}

		switch p.Type {
		case 0: // int → int32
			v := parseInt32(p.ValInt)
			b := make([]byte, 4)
			binary.BigEndian.PutUint32(b, uint32(v))
			buf = append(buf, b...)
		case 1: // double → float32_auto
			v := parseFloat64(p.ValDouble)
			bits := math.Float32bits(float32(v))
			b := make([]byte, 4)
			binary.BigEndian.PutUint32(b, bits)
			buf = append(buf, b...)
		case 2: // enum → int32
			v := parseInt32(p.ValEnum)
			b := make([]byte, 4)
			binary.BigEndian.PutUint32(b, uint32(v))
			buf = append(buf, b...)
		case 3: // string → skip
			continue
		case 4: // bool → uint8
			v := uint8(0)
			if p.ValBool == "1" || p.ValBool == "true" {
				v = 1
			}
			buf = append(buf, v)
		}
	}

	return buf
}

func parseInt32(s string) int32 {
	if s == "" {
		return 0
	}
	v, err := strconv.ParseInt(s, 10, 32)
	if err != nil {
		return 0
	}
	return int32(v)
}

func parseFloat64(s string) float64 {
	if s == "" {
		return 0
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		return 0
	}
	return v
}

// generateDefaultMCConf creates a realistic motor controller config blob.
// This is a simplified version - VESC Tool mainly needs valid data to display.
// The real MC_CONF has 400+ parameters; we generate the commonly accessed ones.
func generateDefaultMCConf() []byte {
	var buf []byte

	// The MC_CONF is a very large blob. VESC Tool will try to parse it
	// but for simulation purposes we can provide a minimal valid config.
	// The key is that the size matches what VESC Tool expects for the FW version.

	// For VESC FW 6.5, MC_CONF is approximately 488 bytes.
	// We generate zeros with realistic values at key offsets.
	buf = make([]byte, 488)

	// pwm_mode at offset 0 (int32) - FOC = 2
	binary.BigEndian.PutUint32(buf[0:], 2)

	// motor_type at offset 8 (int32) - FOC = 2
	binary.BigEndian.PutUint32(buf[8:], 2)

	// l_current_max at offset ~20 (float32_auto) - 65A
	binary.BigEndian.PutUint32(buf[20:], math.Float32bits(65.0))

	// l_current_min at offset ~24 (float32_auto) - -65A
	binary.BigEndian.PutUint32(buf[24:], math.Float32bits(-65.0))

	// l_in_current_max at offset ~28 - 60A
	binary.BigEndian.PutUint32(buf[28:], math.Float32bits(60.0))

	// l_in_current_min at offset ~32 - -30A
	binary.BigEndian.PutUint32(buf[32:], math.Float32bits(-30.0))

	// l_abs_current_max at offset ~36 - 150A
	binary.BigEndian.PutUint32(buf[36:], math.Float32bits(150.0))

	// l_max_erpm at offset ~48 - 100000
	binary.BigEndian.PutUint32(buf[48:], math.Float32bits(100000.0))

	// l_battery_cut_start at offset ~76 - 42V
	binary.BigEndian.PutUint32(buf[76:], math.Float32bits(42.0))

	// l_battery_cut_end at offset ~80 - 40V
	binary.BigEndian.PutUint32(buf[80:], math.Float32bits(40.0))

	return buf
}

// generateDefaultAppConf creates a realistic app config blob.
func generateDefaultAppConf() []byte {
	// For VESC FW 6.5, APP_CONF is approximately 360 bytes.
	buf := make([]byte, 360)

	// controller_id at offset 0 (uint8)
	buf[0] = 0

	// app_to_use at offset 4 (int32) - APP_CUSTOM = 9
	binary.BigEndian.PutUint32(buf[4:], 9)

	return buf
}
