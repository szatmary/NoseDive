import SwiftUI

enum Theme {
    static let background = Color(hex: 0x0A0A0A)
    static let surface = Color(hex: 0x1A1A1A)
    static let surfaceRaised = Color(hex: 0x252525)
    static let primary = Color(hex: 0x4ECDC4)       // teal
    static let warning = Color(hex: 0xFFB347)        // amber
    static let danger = Color(hex: 0xFF6B6B)         // coral
    static let success = Color(hex: 0x7BC67E)        // green
    static let textPrimary = Color.white
    static let textSecondary = Color(hex: 0x888888)
    static let textTertiary = Color(hex: 0x555555)

    static let cardRadius: CGFloat = 12
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
