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
        case eraseNewApp = 2
        case writeNewAppData = 3
        case getValues = 4
        case forwardCAN = 34
        case customAppData = 36
        case getValuesSetup = 47
        case pingCAN = 62
        case getIMUData = 65
        case bmsGetValues = 96
        case getBatteryCut = 115
        case getStats = 128
    }

    enum HWType: UInt8 {
        case vesc = 0
        case vescExpress = 3
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

    // MARK: - FW Version

    struct FWVersionInfo {
        var major: UInt8 = 0
        var minor: UInt8 = 0
        var hwName: String = ""
        var uuid: String = ""
        var hwType: HWType = .vesc
        var customConfigCount: UInt8 = 0
        var packageName: String = ""
        var isPaired: Bool = false
    }

    static func parseFWVersion(_ payload: Data) -> FWVersionInfo? {
        guard payload.count > 3, payload[payload.startIndex] == CommPacketID.fwVersion.rawValue else { return nil }
        var info = FWVersionInfo()
        info.major = payload[payload.startIndex + 1]
        info.minor = payload[payload.startIndex + 2]

        var idx = payload.startIndex + 3

        // HW name (null-terminated)
        var hwChars: [UInt8] = []
        while idx < payload.endIndex && payload[idx] != 0 { hwChars.append(payload[idx]); idx += 1 }
        info.hwName = String(bytes: hwChars, encoding: .utf8) ?? ""
        idx += 1 // skip null

        // UUID (12 bytes)
        if idx + 12 <= payload.endIndex {
            info.uuid = payload[idx..<idx+12].map { String(format: "%02x", $0) }.joined()
            idx += 12
        }

        // isPaired (1 byte)
        if idx < payload.endIndex { info.isPaired = payload[idx] != 0; idx += 1 }
        // FW test version (1 byte)
        if idx < payload.endIndex { idx += 1 }
        // HW type (1 byte)
        if idx < payload.endIndex { info.hwType = HWType(rawValue: payload[idx]) ?? .vesc; idx += 1 }
        // Custom config count (1 byte)
        if idx < payload.endIndex { info.customConfigCount = payload[idx]; idx += 1 }
        // Has phase filters (1 byte)
        if idx < payload.endIndex { idx += 1 }
        // QML HW (1 byte)
        if idx < payload.endIndex { idx += 1 }
        // QML App (1 byte)
        if idx < payload.endIndex { idx += 1 }
        // NRF flags (1 byte)
        if idx < payload.endIndex { idx += 1 }
        // FW/package name (null-terminated)
        var nameChars: [UInt8] = []
        while idx < payload.endIndex && payload[idx] != 0 { nameChars.append(payload[idx]); idx += 1 }
        info.packageName = String(bytes: nameChars, encoding: .utf8) ?? ""

        return info
    }

    // MARK: - CAN Forward

    /// Build a COMM_FORWARD_CAN payload wrapping an inner command for a specific controller.
    static func buildCANForward(targetID: UInt8, innerPayload: Data) -> Data {
        var payload = Data([CommPacketID.forwardCAN.rawValue, targetID])
        payload.append(innerPayload)
        return payload
    }

    /// Build a COMM_FW_VERSION request for a specific CAN device.
    static func buildFWVersionRequestForCAN(targetID: UInt8) -> Data {
        return buildCANForward(targetID: targetID, innerPayload: Data([CommPacketID.fwVersion.rawValue]))
    }

    // MARK: - Refloat custom app

    static let refloatMagic: UInt8 = 0x65

    /// Build a Refloat info request (COMM_CUSTOM_APP_DATA + magic + CommandInfo).
    static func buildRefloatInfoRequest() -> Data {
        return Data([CommPacketID.customAppData.rawValue, refloatMagic, 0x00]) // CommandInfo = 0
    }

    struct RefloatInfo {
        var name: String = ""
        var major: UInt8 = 0
        var minor: UInt8 = 0
        var patch: UInt8 = 0
        var suffix: String = ""

        var versionString: String {
            var s = "\(major).\(minor).\(patch)"
            if !suffix.isEmpty { s += "-\(suffix)" }
            return s
        }
    }

    /// Parse a Refloat info response. Payload: [cmd=36][magic=0x65][cmdId=0][version=2]...
    static func parseRefloatInfo(_ payload: Data) -> RefloatInfo? {
        guard payload.count > 5,
              payload[payload.startIndex] == CommPacketID.customAppData.rawValue,
              payload[payload.startIndex + 1] == refloatMagic,
              payload[payload.startIndex + 2] == 0x00 else { return nil }

        var info = RefloatInfo()
        let version = payload[payload.startIndex + 3]
        if version >= 2 {
            // v2 format: [version][major][minor][patch][name:20 bytes][suffix:20 bytes]...
            guard payload.count >= payload.startIndex + 4 + 3 + 40 else { return nil }
            info.major = payload[payload.startIndex + 4]
            info.minor = payload[payload.startIndex + 5]
            info.patch = payload[payload.startIndex + 6]

            let nameStart = payload.startIndex + 7
            let nameBytes = payload[nameStart..<nameStart+20]
            info.name = String(bytes: nameBytes.prefix(while: { $0 != 0 }), encoding: .utf8) ?? ""

            let suffixStart = nameStart + 20
            let suffixBytes = payload[suffixStart..<suffixStart+20]
            info.suffix = String(bytes: suffixBytes.prefix(while: { $0 != 0 }), encoding: .utf8) ?? ""
        }

        return info
    }
}
