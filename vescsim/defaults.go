package main

import (
	"bytes"
	"compress/zlib"
	_ "embed"
	"encoding/xml"
	"log"
	"strconv"

)

//go:embed refloat_settings.xml
var refloatConfigXML []byte

//go:embed mcconf_default.xml
var mcconfDefaultXML []byte

//go:embed appconf_default.xml
var appconfDefaultXML []byte

//go:embed refloat_ui.qml
var refloatQMLRaw []byte

// compressedRefloatQML is the Qt qCompress format: 4-byte big-endian
// uncompressed size + zlib data. This matches qUncompress() in VESC Tool.
var compressedRefloatQML []byte

func init() {
	compressedRefloatQML = qtCompress(refloatQMLRaw)
}

// qtCompress produces Qt's qCompress format: [uncompressed_size:4BE][zlib_data]
func qtCompress(data []byte) []byte {
	var buf bytes.Buffer
	// 4-byte big-endian uncompressed size (Qt format)
	size := uint32(len(data))
	buf.WriteByte(byte(size >> 24))
	buf.WriteByte(byte(size >> 16))
	buf.WriteByte(byte(size >> 8))
	buf.WriteByte(byte(size))
	// zlib compressed data
	w := zlib.NewWriter(&buf)
	w.Write(data)
	w.Close()
	return buf.Bytes()
}

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
// Integer types (type=0, type=2) use vTx (VESC_TX_T enum) for wire size:
//
//   - vTx=1 UINT8: 1 byte
//   - vTx=2 INT8: 1 byte
//   - vTx=3 UINT16: 2 bytes big-endian
//   - vTx=4 INT16: 2 bytes big-endian
//   - vTx=5 UINT32: 4 bytes big-endian
//   - vTx=6 INT32: 4 bytes big-endian
//
// Double types (type=1) use vTx for encoding:
//
//   - vTx=7 DOUBLE16: 2 bytes, int16(val * vTxDoubleScale)
//   - vTx=8 DOUBLE32: 4 bytes, int32(val * vTxDoubleScale)
//   - vTx=9 DOUBLE32_AUTO: 4 bytes, IEEE754 float32 (self-describing)
//
// Other types:
//
//   - type=4 (enum): uint8 (1 byte)
//   - type=5 (bool): uint8 (1 byte)
//   - type=6 (bitfield): uint8 (1 byte)
//   - type=3 (string): skipped
func generateDefaultConfig(xmlData []byte, signature uint32) []byte {
	var cfg configParams
	if err := xml.Unmarshal(xmlData, &cfg); err != nil {
		log.Printf("simulator: failed to parse config XML: %v", err)
		return nil
	}

	// Start with 4-byte signature
	var buf []byte
	buf = AppendUint32(buf, signature)

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
			case 1: // UINT8
				buf = append(buf, uint8(parseInt32(p.ValInt)))
			case 2: // INT8
				buf = append(buf, uint8(int8(parseInt32(p.ValInt))))
			case 3: // UINT16
				buf = AppendUint16(buf, uint16(parseInt32(p.ValInt)))
			case 4: // INT16
				buf = AppendInt16(buf, int16(parseInt32(p.ValInt)))
			case 5: // UINT32
				buf = AppendUint32(buf, uint32(parseInt32(p.ValInt)))
			case 6: // INT32
				buf = AppendInt32(buf, parseInt32(p.ValInt))
			default: // default: uint8
				buf = append(buf, uint8(parseInt32(p.ValInt)))
			}
		case 1: // double
			switch vtx {
			case 7: // DOUBLE16: int16(val * scale), 2 bytes
				scale := parseFloat64(p.VTxDoubleScale)
				buf = AppendInt16(buf, int16(parseFloat64(p.ValDouble)*scale))
			case 8: // DOUBLE32: int32(val * scale), 4 bytes
				scale := parseFloat64(p.VTxDoubleScale)
				buf = AppendInt32(buf, int32(parseFloat64(p.ValDouble)*scale))
			case 9: // DOUBLE32_AUTO: IEEE754 float32, 4 bytes
				buf = AppendFloat32Auto(buf, parseFloat64(p.ValDouble))
			default: // fallback: float32_auto
				buf = AppendFloat32Auto(buf, parseFloat64(p.ValDouble))
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

// FW 6.05 config signatures from confgenerator.h (vedderb/bldc release_6_05)
const (
	MCConfSignature605  uint32 = 1065524471
	AppConfSignature605 uint32 = 2099347128
)

// generateDefaultMCConf creates a motor controller config blob from the
// embedded MC_CONF XML (parameters_mcconf.xml for FW 6.05).
func generateDefaultMCConf() []byte {
	return generateDefaultConfig(mcconfDefaultXML, MCConfSignature605)
}

// generateDefaultAppConf creates an app config blob from the
// embedded APP_CONF XML (parameters_appconf.xml for FW 6.05).
func generateDefaultAppConf() []byte {
	return generateDefaultConfig(appconfDefaultXML, AppConfSignature605)
}
