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

}
