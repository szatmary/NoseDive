import Foundation

/// Minimal VESC types still referenced by SwiftUI views.
/// All packet framing, parsing, and command building is handled by the C++ engine.
enum VESCPacket {

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
