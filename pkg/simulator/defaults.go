package simulator

import (
	"bytes"
	"compress/zlib"
	_ "embed"
	"encoding/binary"
	"encoding/xml"
	"log"
	"strconv"

	"github.com/szatmary/nosedive/pkg/vesc"
)

//go:embed refloat_settings.xml
var refloatConfigXML []byte

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
	XMLName       xml.Name
	LongName      string `xml:"longName"`
	Type          int    `xml:"type"`
	Transmittable *int   `xml:"transmittable"`
	VTx           *int   `xml:"vTx"`
	ValDouble     string `xml:"valDouble"`
	ValInt        string `xml:"valInt"`
	ValString     string `xml:"valString"`
	ValBool       string `xml:"valBool"`
	ValEnum       string `xml:"valEnum"`
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

// generateDefaultConfig parses the Refloat settings XML and serializes
// default values in the same order/format as VESC's confparser.
//
// The binary format starts with a 4-byte SIGNATURE (CRC of XML content),
// followed by each transmittable field serialized according to its vTx type:
//
//   - vTx=1 (uint8): 1 byte
//   - vTx=2 (int8): 1 byte
//   - vTx=3 (uint16): 2 bytes big-endian
//   - vTx=7 (float32_auto): 5 bytes (1 scale + 4 int32)
//   - bool/enum with no vTx: 1 byte (uint8)
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
		case 0: // int
			switch vtx {
			case 1: // uint8
				buf = append(buf, uint8(parseInt32(p.ValInt)))
			case 2: // int8
				buf = append(buf, uint8(int8(parseInt32(p.ValInt))))
			case 3: // uint16
				buf = vesc.AppendUint16(buf, uint16(parseInt32(p.ValInt)))
			case 7: // float32_auto
				buf = vesc.AppendFloat32Auto(buf, parseFloat64(p.ValInt))
			default: // default: uint8 for small ints
				buf = append(buf, uint8(parseInt32(p.ValInt)))
			}
		case 1: // double → float32_auto
			buf = vesc.AppendFloat32Auto(buf, parseFloat64(p.ValDouble))
		case 2: // enum → uint8
			buf = append(buf, uint8(parseInt32(p.ValEnum)))
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

// generateDefaultMCConf creates a motor controller config blob.
// The real MC_CONF uses the confparser-generated serialization format with
// a signature CRC prefix. We generate a zeroed blob of the right size.
// VESC Tool may fail to parse it (signature mismatch), which is acceptable
// for simulation since the Refloat custom config is what matters.
func generateDefaultMCConf() []byte {
	// For VESC FW 6.5, MC_CONF is approximately 488 bytes
	buf := make([]byte, 488)
	// Signature at offset 0 (4 bytes) - dummy
	binary.BigEndian.PutUint32(buf[0:], 0)
	return buf
}

// generateDefaultAppConf creates an app config blob.
func generateDefaultAppConf() []byte {
	// For VESC FW 6.5, APP_CONF is approximately 360 bytes
	buf := make([]byte, 360)
	binary.BigEndian.PutUint32(buf[0:], 0)
	return buf
}
