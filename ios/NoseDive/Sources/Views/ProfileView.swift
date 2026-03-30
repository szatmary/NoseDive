import SwiftUI

struct ProfileView: View {
    @EnvironmentObject var boardManager: BoardManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    boardSection
                    unitsSection
                    aboutSection
                }
                .padding()
            }
            .background(Theme.background)
            .navigationTitle("Me")
        }
    }

    // MARK: - Board Config

    private var boardSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            sectionHeader("Board Settings")

            if let board = boardManager.activeBoard {
                configRow("Name", board.name)
                configRow("Pole Pairs", "\(board.motorPolePairs)")
                configRow("Wheel Circumference", String(format: "%.3f m", board.wheelCircumferenceM))
                configRow("Battery Cells", "\(board.batterySeriesCells)S")
                configRow("Voltage Range",
                          String(format: "%.1f – %.1fV", board.batteryVoltageMin, board.batteryVoltageMax))
            } else {
                HStack {
                    Text("No board connected")
                        .font(.subheadline)
                        .foregroundStyle(Theme.textSecondary)
                    Spacer()
                }
            }
        }
        .card()
    }

    // MARK: - Units

    private var unitsSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            sectionHeader("Preferences")
            configRow("Speed Units", "MPH")
            configRow("Temperature", "°F")
            configRow("Distance", "Miles")
        }
        .card()
    }

    // MARK: - About

    private var aboutSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            sectionHeader("About")
            configRow("App Version", "0.1.0")
            configRow("Backend", "libnosedive (C++)")

            HStack {
                Text("NoseDive is an open-source companion app for VESC-based onewheel boards.")
                    .font(.caption)
                    .foregroundStyle(Theme.textTertiary)
            }
        }
        .card()
    }

    // MARK: - Helpers

    private func sectionHeader(_ title: String) -> some View {
        Text(title)
            .font(.headline)
            .foregroundStyle(Theme.textPrimary)
    }

    private func configRow(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label)
                .font(.subheadline)
                .foregroundStyle(Theme.textSecondary)
            Spacer()
            Text(value)
                .font(.subheadline)
                .foregroundStyle(Theme.textPrimary)
        }
    }
}
