import SwiftUI

enum Theme {
    // Core palette — white + hot pink, HDR-wide gamut where supported
    static let background = Color(hex: 0xFCFCFC)
    static let surface = Color.white
    static let surfaceRaised = Color(hex: 0xF2F2F2)

    // Hot pink — use extended sRGB to push into HDR on supported displays
    static let primary = Color(.displayP3, red: 1.0, green: 0.05, blue: 0.4, opacity: 1.0)
    static let primarySoft = Color(.displayP3, red: 1.0, green: 0.05, blue: 0.4, opacity: 0.12)

    // Vivid accent colors in P3 gamut
    static let warning = Color(.displayP3, red: 1.0, green: 0.35, blue: 0.0, opacity: 1.0)
    static let danger = Color(.displayP3, red: 1.0, green: 0.1, blue: 0.1, opacity: 1.0)
    static let success = Color(.displayP3, red: 0.0, green: 0.75, blue: 0.35, opacity: 1.0)

    // Text on white
    static let textPrimary = Color(hex: 0x1A1A1A)
    static let textSecondary = Color(hex: 0x6E6E6E)
    static let textTertiary = Color(hex: 0xBBBBBB)

    static let cardRadius: CGFloat = 16
    static let cardPadding: CGFloat = 16

    static func dutyColor(_ duty: Double) -> Color {
        let d = abs(duty)
        if d < 0.5 { return success }
        if d < 0.7 { return primary }
        if d < 0.85 { return warning }
        return danger
    }

    static func batteryColor(_ percent: Double) -> Color {
        if percent > 40 { return success }
        if percent > 20 { return warning }
        return danger
    }
}

extension Color {
    init(hex: UInt32) {
        self.init(
            red: Double((hex >> 16) & 0xFF) / 255.0,
            green: Double((hex >> 8) & 0xFF) / 255.0,
            blue: Double(hex & 0xFF) / 255.0
        )
    }
}
