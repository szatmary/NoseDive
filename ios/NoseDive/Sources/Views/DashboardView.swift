import SwiftUI

struct DashboardView: View {
    @EnvironmentObject var boardManager: BoardManager

    var body: some View {
        NavigationStack {
            ScrollView {
                if boardManager.isConnected {
                    connectedDashboard
                } else {
                    notConnectedView
                }
            }
            .background(Theme.background)
            .navigationTitle("Dashboard")
        }
    }

    // MARK: - Connected Dashboard

    private var connectedDashboard: some View {
        VStack(spacing: 16) {
            speedHero
            dutyBatteryRow
            telemetryGrid
            footpadIndicator
        }
        .padding()
    }

    // MARK: - Speed Hero

    private var speedHero: some View {
        VStack(spacing: 4) {
            Text(String(format: "%.1f", boardManager.speedMph))
                .font(.system(size: 72, weight: .bold, design: .rounded))
                .foregroundStyle(Theme.textPrimary)
                .contentTransition(.numericText())
            Text("MPH")
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)

            Text(boardManager.refloatState.runState.description)
                .font(.subheadline.bold())
                .foregroundStyle(boardManager.refloatState.runState.color)
                .padding(.top, 4)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 24)
        .card()
    }

    // MARK: - Duty & Battery Row

    private var dutyBatteryRow: some View {
        HStack(spacing: 16) {
            // Duty cycle
            VStack(spacing: 8) {
                Text("Duty")
                    .font(.caption)
                    .foregroundStyle(Theme.textSecondary)
                ZStack {
                    Circle()
                        .stroke(Theme.surfaceRaised, lineWidth: 6)
                    Circle()
                        .trim(from: 0, to: abs(boardManager.telemetry.dutyCycle))
                        .stroke(
                            Theme.dutyColor(boardManager.telemetry.dutyCycle),
                            style: StrokeStyle(lineWidth: 6, lineCap: .round)
                        )
                        .rotationEffect(.degrees(-90))
                    Text("\(Int(boardManager.telemetry.dutyCycle * 100))%")
                        .font(.title3.bold())
                        .foregroundStyle(Theme.textPrimary)
                }
                .frame(width: 80, height: 80)
            }
            .frame(maxWidth: .infinity)
            .card()

            // Battery
            VStack(spacing: 8) {
                Text("Battery")
                    .font(.caption)
                    .foregroundStyle(Theme.textSecondary)
                ZStack {
                    Circle()
                        .stroke(Theme.surfaceRaised, lineWidth: 6)
                    Circle()
                        .trim(from: 0, to: boardManager.telemetry.batteryPercent / 100)
                        .stroke(
                            Theme.batteryColor(boardManager.telemetry.batteryPercent),
                            style: StrokeStyle(lineWidth: 6, lineCap: .round)
                        )
                        .rotationEffect(.degrees(-90))
                    Text("\(Int(boardManager.telemetry.batteryPercent))%")
                        .font(.title3.bold())
                        .foregroundStyle(Theme.textPrimary)
                }
                .frame(width: 80, height: 80)
                Text(String(format: "%.1fV", boardManager.telemetry.batteryVoltage))
                    .font(.caption)
                    .foregroundStyle(Theme.textTertiary)
            }
            .frame(maxWidth: .infinity)
            .card()
        }
    }

    // MARK: - Telemetry Grid

    private var telemetryGrid: some View {
        LazyVGrid(columns: [
            GridItem(.flexible()),
            GridItem(.flexible()),
            GridItem(.flexible())
        ], spacing: 12) {
            telemetryCell("Motor", String(format: "%.1fA", boardManager.telemetry.motorCurrent), Theme.primary)
            telemetryCell("Battery", String(format: "%.1fA", boardManager.telemetry.batteryCurrent), Theme.primary)
            telemetryCell("Power", String(format: "%.0fW", boardManager.telemetry.power), Theme.primary)
            telemetryCell("MOSFET", String(format: "%.0f°C", boardManager.telemetry.mosfetTemp), tempColor(boardManager.telemetry.mosfetTemp))
            telemetryCell("Motor", String(format: "%.0f°C", boardManager.telemetry.motorTemp), tempColor(boardManager.telemetry.motorTemp))
            telemetryCell("Trip", String(format: "%.1f mi", boardManager.telemetry.tripDistance * 0.000621371), Theme.textPrimary)
            telemetryCell("Pitch", String(format: "%.1f°", boardManager.telemetry.pitch), Theme.textPrimary)
            telemetryCell("Roll", String(format: "%.1f°", boardManager.telemetry.roll), Theme.textPrimary)
            telemetryCell("ERPM", String(format: "%.0f", boardManager.telemetry.erpm), Theme.textPrimary)
        }
    }

    private func telemetryCell(_ label: String, _ value: String, _ color: Color) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.subheadline.bold())
                .foregroundStyle(color)
            Text(label)
                .font(.caption2)
                .foregroundStyle(Theme.textSecondary)
        }
        .frame(maxWidth: .infinity)
        .card()
    }

    private func tempColor(_ temp: Double) -> Color {
        if temp < 50 { return Theme.success }
        if temp < 70 { return Theme.warning }
        return Theme.danger
    }

    // MARK: - Footpad

    private var footpadIndicator: some View {
        HStack(spacing: 16) {
            Text("Footpad")
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)
            Spacer()
            HStack(spacing: 8) {
                footpadSide("L", active: boardManager.refloatState.footpad == .left || boardManager.refloatState.footpad == .both)
                footpadSide("R", active: boardManager.refloatState.footpad == .right || boardManager.refloatState.footpad == .both)
            }
        }
        .card()
    }

    private func footpadSide(_ label: String, active: Bool) -> some View {
        Text(label)
            .font(.caption.bold())
            .foregroundStyle(active ? .white : Theme.textTertiary)
            .frame(width: 32, height: 24)
            .background(active ? Theme.primary : Theme.surfaceRaised)
            .clipShape(RoundedRectangle(cornerRadius: 6))
    }

    // MARK: - Not Connected

    private var notConnectedView: some View {
        VStack(spacing: 16) {
            Spacer(minLength: 80)
            Image(systemName: "gauge.open.with.lines.needle.33percent")
                .font(.system(size: 64))
                .foregroundStyle(Theme.textTertiary)
            Text("Connect a board to see live telemetry")
                .font(.subheadline)
                .foregroundStyle(Theme.textSecondary)
            Spacer(minLength: 80)
        }
        .frame(maxWidth: .infinity)
    }
}
