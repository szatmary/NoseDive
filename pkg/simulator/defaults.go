package simulator

import (
	"bytes"
	"compress/zlib"
	_ "embed"
	"encoding/xml"
	"log"
	"strconv"

	"github.com/szatmary/nosedive/pkg/vesc"
)

//go:embed refloat_settings.xml
var refloatConfigXML []byte

//go:embed mcconf_default.xml
var mcconfDefaultXML []byte

//go:embed appconf_default.xml
var appconfDefaultXML []byte

// compressConfigXML compresses the XML using zlib and prepends the
// 3-byte big-endian uncompressed length, matching VESC's confxml.c wire format.
func compressConfigXML(xmlData []byte) []byte {
	var buf bytes.Buffer

	// 3-byte big-endian uncompressed length
	uncompLen := len(xmlData)
	buf.WriteByte(byte(uncompLen >> 16))
	buf.WriteByte(byte(uncompLen >> 8))
	buf.WriteByte(byte(uncompLen))

	// Zlib compress
	w := zlib.NewWriter(&buf)
	w.Write(xmlData)
	w.Close()

	return buf.Bytes()
}

// configParam represents a single parameter from the VESC config XML.
type configParam struct {
	XMLName        xml.Name
	LongName       string `xml:"longName"`
	Type           int    `xml:"type"`
	Transmittable  *int   `xml:"transmittable"`
	VTx            *int   `xml:"vTx"`
	VTxDoubleScale string `xml:"vTxDoubleScale"`
	ValDouble      string `xml:"valDouble"`
	ValInt         string `xml:"valInt"`
	ValString      string `xml:"valString"`
	ValBool        string `xml:"valBool"`
	ValEnum        string `xml:"valEnum"`
}

// configParams wraps the XML. Go's encoding/xml doesn't support wildcard
// child elements, so we use a custom unmarshaler to collect all children of <Params>.
type configParams struct {
	Params []configParam
}

func (c *configParams) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	// Find <Params> element
	for {
		tok, err := d.Token()
		if err != nil {
			return err
		}
		se, ok := tok.(xml.StartElement)
		if ok && se.Name.Local == "Params" {
			// Read all children of <Params>
			for {
				tok2, err := d.Token()
				if err != nil {
					return err
				}
				switch t := tok2.(type) {
				case xml.StartElement:
					var p configParam
					if err := d.DecodeElement(&p, &t); err != nil {
						return err
					}
					c.Params = append(c.Params, p)
				case xml.EndElement:
					if t.Name.Local == "Params" {
						return d.Skip() // skip rest of ConfigParams
					}
				}
			}
		}
	}
}

// generateDefaultConfig parses a VESC config XML and serializes default
// values in the same order/format as VESC's confparser/confgenerator.
//
// The binary format starts with a 4-byte SIGNATURE (CRC of XML content),
// followed by each transmittable field serialized according to its type/vTx:
//
// Integer types (type=0, type=2) use vTx to determine wire size:
//
//   - vTx=1: uint8 (1 byte)
//   - vTx=2: int8 (1 byte)
//   - vTx=3: uint16 (2 bytes)
//   - vTx=4: int16 (2 bytes)
//   - vTx=5: int32 (4 bytes)
//   - vTx=6: uint32 (4 bytes)
//   - vTx=7: float32_auto (4 bytes, IEEE754)
//
// Double types (type=1) use vTx to determine encoding:
//
//   - vTx=7: float32_auto (4 bytes, IEEE754)
//   - vTx=8: float16 scaled (2 bytes, int16(val*scale))
//   - vTx=9: float32 scaled (4 bytes, int32(val*scale))
//
// Other types:
//
//   - type=4 (enum): uint8 (1 byte)
//   - type=5 (bool): uint8 (1 byte)
//   - type=6 (bitfield): uint8 (1 byte)
//   - type=3 (string): skipped
func generateDefaultConfig(xmlData []byte) []byte {
	var cfg configParams
	if err := xml.Unmarshal(xmlData, &cfg); err != nil {
		log.Printf("simulator: failed to parse config XML: %v", err)
		return nil
	}

	// Start with 4-byte signature (CRC of XML)
	sig := vesc.CRC16(xmlData)
	var buf []byte
	buf = vesc.AppendUint32(buf, uint32(sig)<<16|uint32(sig))

	for _, p := range cfg.Params {
		// Skip non-transmittable params
		if p.Transmittable != nil && *p.Transmittable == 0 {
			continue
		}

		vtx := 0
		if p.VTx != nil {
			vtx = *p.VTx
		}

		switch p.Type {
		case 0, 2: // int
			switch vtx {
			case 1: // uint8
				buf = append(buf, uint8(parseInt32(p.ValInt)))
			case 2: // int8
				buf = append(buf, uint8(int8(parseInt32(p.ValInt))))
			case 3: // uint16
				buf = vesc.AppendUint16(buf, uint16(parseInt32(p.ValInt)))
			case 4: // int16
				buf = vesc.AppendInt16(buf, int16(parseInt32(p.ValInt)))
			case 5: // int32
				buf = vesc.AppendInt32(buf, parseInt32(p.ValInt))
			case 6: // uint32
				buf = vesc.AppendUint32(buf, uint32(parseInt32(p.ValInt)))
			case 7: // float32_auto
				buf = vesc.AppendFloat32Auto(buf, parseFloat64(p.ValInt))
			default: // default: uint8
				buf = append(buf, uint8(parseInt32(p.ValInt)))
			}
		case 1: // double
			switch vtx {
			case 8: // float16 scaled: int16(val * scale)
				scale := parseFloat64(p.VTxDoubleScale)
				buf = vesc.AppendInt16(buf, int16(parseFloat64(p.ValDouble)*scale))
			case 9: // float32 scaled: int32(val * scale)
				scale := parseFloat64(p.VTxDoubleScale)
				buf = vesc.AppendInt32(buf, int32(parseFloat64(p.ValDouble)*scale))
			default: // vTx=7 or unspecified: float32_auto (IEEE754)
				buf = vesc.AppendFloat32Auto(buf, parseFloat64(p.ValDouble))
			}
		case 3: // string → skip
			continue
		case 4: // enum → uint8
			buf = append(buf, uint8(parseInt32(p.ValEnum)))
		case 5: // bool → uint8
			v := uint8(0)
			if p.ValBool == "1" || p.ValBool == "true" {
				v = 1
			}
			buf = append(buf, v)
		case 6: // bitfield → uint8
			buf = append(buf, uint8(parseInt32(p.ValInt)))
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

// generateDefaultMCConf creates a motor controller config blob from the
// embedded MC_CONF XML (parameters_mcconf.xml for FW 6.05).
func generateDefaultMCConf() []byte {
	return generateDefaultConfig(mcconfDefaultXML)
}

// generateDefaultAppConf creates an app config blob from the
// embedded APP_CONF XML (parameters_appconf.xml for FW 6.05).
func generateDefaultAppConf() []byte {
	return generateDefaultConfig(appconfDefaultXML)
}
