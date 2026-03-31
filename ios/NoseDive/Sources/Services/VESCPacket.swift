import Foundation

/// VESC packet framing and CRC in pure Swift.
/// Short: [0x02][len:1][payload][crc16:2][0x03]
/// Long:  [0x03][len:2][payload][crc16:2][0x03]
enum VESCPacket {

    // MARK: - CRC16

    private static let crcTable: [UInt16] = {
        var table = [UInt16](repeating: 0, count: 256)
        for i in 0..<256 {
            var crc = UInt16(i) << 8
            for _ in 0..<8 {
                if crc & 0x8000 != 0 {
                    crc = (crc << 1) ^ 0x1021
                } else {
                    crc <<= 1
                }
            }
            table[i] = crc
        }
        return table
    }()

    static func crc16(_ data: Data) -> UInt16 {
        var crc: UInt16 = 0
        for byte in data {
            crc = (crc << 8) ^ crcTable[Int((crc >> 8) ^ UInt16(byte))]
        }
        return crc
    }

    // MARK: - Encode

    /// Frame a payload into a VESC packet.
    static func encode(_ payload: Data) -> Data {
        let len = payload.count
        var packet = Data()
        if len < 256 {
            packet.append(0x02)
            packet.append(UInt8(len))
        } else {
            packet.append(0x03)
            packet.append(UInt8((len >> 8) & 0xFF))
            packet.append(UInt8(len & 0xFF))
        }
        packet.append(payload)
        let crc = crc16(payload)
        packet.append(UInt8((crc >> 8) & 0xFF))
        packet.append(UInt8(crc & 0xFF))
        packet.append(0x03)
        return packet
    }

    // MARK: - Decode

    /// Try to extract a complete packet from a buffer.
    /// Returns (payload, bytesConsumed) or nil if no complete packet yet.
    static func decode(_ buffer: Data) -> (payload: Data, consumed: Int)? {
        guard buffer.count >= 5 else { return nil } // minimum: start + len + 1byte + crc(2) + end

        let start = buffer[buffer.startIndex]
        guard start == 0x02 || start == 0x03 else { return nil }

        let isShort = start == 0x02
        let headerLen = isShort ? 2 : 3
        guard buffer.count >= headerLen else { return nil }

        let payloadLen: Int
        if isShort {
            payloadLen = Int(buffer[buffer.startIndex + 1])
        } else {
            payloadLen = Int(buffer[buffer.startIndex + 1]) << 8 | Int(buffer[buffer.startIndex + 2])
        }

        let totalLen = headerLen + payloadLen + 3 // header + payload + crc(2) + end(1)
        guard buffer.count >= totalLen else { return nil }

        // End byte
        guard buffer[buffer.startIndex + totalLen - 1] == 0x03 else { return nil }

        // Extract payload
        let payloadStart = buffer.startIndex + headerLen
        let payload = buffer[payloadStart..<payloadStart + payloadLen]

        // CRC check
        let crcOffset = payloadStart + payloadLen
        let receivedCRC = UInt16(buffer[crcOffset]) << 8 | UInt16(buffer[crcOffset + 1])
        let computedCRC = crc16(Data(payload))
        guard receivedCRC == computedCRC else { return nil }

        return (Data(payload), totalLen)
    }

    // MARK: - Command IDs

    enum CommPacketID: UInt8 {
        case fwVersion = 0
        case getValues = 4
        case forwardCAN = 34
        case customAppData = 36
        case getValuesSetup = 47
        case pingCAN = 62
        case getIMUData = 65
        case getBatteryCut = 115
        case getStats = 128
    }

    // MARK: - Buffer helpers (big-endian)

    static func getFloat16(_ data: Data, at offset: Int, scale: Double) -> Double {
        guard offset + 1 < data.count else { return 0 }
        let raw = Int16(bitPattern: UInt16(data[data.startIndex + offset]) << 8 | UInt16(data[data.startIndex + offset + 1]))
        return Double(raw) / scale
    }

    static func getFloat32(_ data: Data, at offset: Int, scale: Double) -> Double {
        guard offset + 3 < data.count else { return 0 }
        let s = data.startIndex + offset
        let raw = Int32(bitPattern:
            UInt32(data[s]) << 24 | UInt32(data[s+1]) << 16 |
            UInt32(data[s+2]) << 8 | UInt32(data[s+3]))
        return Double(raw) / scale
    }

    static func getInt32(_ data: Data, at offset: Int) -> Int32 {
        guard offset + 3 < data.count else { return 0 }
        let s = data.startIndex + offset
        return Int32(bitPattern:
            UInt32(data[s]) << 24 | UInt32(data[s+1]) << 16 |
            UInt32(data[s+2]) << 8 | UInt32(data[s+3]))
    }

    // MARK: - Parse COMM_GET_VALUES response

    /// Parse a COMM_GET_VALUES (cmd=4) response payload into BoardTelemetry.
    static func parseValues(_ payload: Data) -> BoardTelemetry? {
        // payload[0] is the command ID, actual data starts at [1]
        guard payload.count >= 70, payload[payload.startIndex] == CommPacketID.getValues.rawValue else { return nil }
        let d = payload.dropFirst() // skip cmd byte
        var t = BoardTelemetry()
        t.mosfetTemp = getFloat16(Data(d), at: 0, scale: 10)
        t.motorTemp = getFloat16(Data(d), at: 2, scale: 10)
        t.motorCurrent = getFloat32(Data(d), at: 4, scale: 100)
        t.batteryCurrent = getFloat32(Data(d), at: 8, scale: 100)
        // skip avg_id(4), avg_iq(4)
        t.dutyCycle = getFloat16(Data(d), at: 20, scale: 1000)
        t.erpm = getFloat32(Data(d), at: 22, scale: 1)
        t.batteryVoltage = getFloat16(Data(d), at: 26, scale: 10)
        // skip amp_hours(4), amp_hours_charged(4), watt_hours(4), watt_hours_charged(4)
        t.tachometer = getInt32(Data(d), at: 44)
        t.tachometerAbs = getInt32(Data(d), at: 48)
        t.fault = Data(d).count > 52 ? Data(d)[Data(d).startIndex + 52] : 0
        // controller_id at offset 56 after pid_pos(4)
        t.power = t.batteryVoltage * t.batteryCurrent
        return t
    }

    // MARK: - Parse COMM_PING_CAN response

    static func parsePingCAN(_ payload: Data) -> [UInt8] {
        guard payload.count >= 2, payload[payload.startIndex] == CommPacketID.pingCAN.rawValue else { return [] }
        return Array(payload.dropFirst())
    }
}
