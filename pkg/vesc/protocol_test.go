package vesc

import (
	"bytes"
	"math"
	"testing"
)

func TestCRC16(t *testing.T) {
	// Known test vector: "123456789" should give 0x31C3 for CRC-CCITT/XModem
	data := []byte("123456789")
	got := CRC16(data)
	want := uint16(0x31C3)
	if got != want {
		t.Errorf("CRC16(%q) = 0x%04X, want 0x%04X", data, got, want)
	}
}

func TestEncodeDecodeShortPacket(t *testing.T) {
	payload := []byte{byte(CommGetValues)}
	encoded, err := EncodePacket(payload)
	if err != nil {
		t.Fatal(err)
	}

	// Verify structure: [0x02][len][payload][crc16_hi][crc16_lo][0x03]
	if encoded[0] != 0x02 {
		t.Errorf("start byte = 0x%02x, want 0x02", encoded[0])
	}
	if encoded[1] != 1 {
		t.Errorf("length = %d, want 1", encoded[1])
	}
	if encoded[len(encoded)-1] != 0x03 {
		t.Errorf("end byte = 0x%02x, want 0x03", encoded[len(encoded)-1])
	}

	// Decode
	r := bytes.NewReader(encoded)
	decoded, err := DecodePacket(r)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(decoded, payload) {
		t.Errorf("decoded = %v, want %v", decoded, payload)
	}
}

func TestEncodeDecodeLongPacket(t *testing.T) {
	payload := make([]byte, 300)
	for i := range payload {
		payload[i] = byte(i)
	}

	encoded, err := EncodePacket(payload)
	if err != nil {
		t.Fatal(err)
	}

	if encoded[0] != 0x03 {
		t.Errorf("start byte = 0x%02x, want 0x03", encoded[0])
	}

	r := bytes.NewReader(encoded)
	decoded, err := DecodePacket(r)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(decoded, payload) {
		t.Errorf("decoded payload mismatch")
	}
}

func TestBufferHelpers(t *testing.T) {
	var buf []byte
	buf = AppendInt16(buf, -1234)
	buf = AppendUint16(buf, 5678)
	buf = AppendInt32(buf, -123456)
	buf = AppendUint32(buf, 654321)

	idx := 0
	if v := GetInt16(buf, &idx); v != -1234 {
		t.Errorf("GetInt16 = %d, want -1234", v)
	}
	if v := GetUint16(buf, &idx); v != 5678 {
		t.Errorf("GetUint16 = %d, want 5678", v)
	}
	if v := GetInt32(buf, &idx); v != -123456 {
		t.Errorf("GetInt32 = %d, want -123456", v)
	}
	if v := GetUint32(buf, &idx); v != 654321 {
		t.Errorf("GetUint32 = %d, want 654321", v)
	}
}

func TestFloat32Auto(t *testing.T) {
	tests := []struct {
		name string
		val  float64
	}{
		{"zero", 0.0},
		{"tiny", 0.00001},
		{"one", 1.0},
		{"negative", -3.14},
		{"large", 10000.0},
		{"small_frac", 0.001},
		{"voltage", 63.5},
		{"current", -25.7},
		{"erpm", 50000.0},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			buf := AppendFloat32Auto(nil, tt.val)
			if len(buf) != 4 {
				t.Fatalf("AppendFloat32Auto produced %d bytes, want 4", len(buf))
			}

			idx := 0
			got := GetFloat32Auto(buf, &idx)
			if idx != 4 {
				t.Fatalf("GetFloat32Auto consumed %d bytes, want 4", idx)
			}

			if tt.val == 0 {
				if got != 0 {
					t.Errorf("round-trip(0) = %v, want 0", got)
				}
				return
			}

			// Allow small relative error due to int32 quantization
			relErr := math.Abs(got-tt.val) / math.Abs(tt.val)
			if relErr > 1e-6 {
				t.Errorf("round-trip(%v) = %v, relative error %v", tt.val, got, relErr)
			}
		})
	}
}
