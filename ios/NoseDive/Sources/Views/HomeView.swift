import SwiftUI

struct HomeView: View {
    @EnvironmentObject var boardManager: BoardManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if boardManager.isConnected, let board = boardManager.activeBoard {
                        connectedCard(board)
                    } else {
                        disconnectedCard
                    }

                    if !boardManager.boards.isEmpty {
                        fleetSection
                    }
                }
                .padding()
            }
            .background(Theme.background)
            .navigationTitle("NoseDive")
        }
    }

    // MARK: - Connected

    private func connectedCard(_ board: Board) -> some View {
        VStack(spacing: 12) {
            HStack {
                Circle()
                    .fill(Theme.success)
                    .frame(width: 10, height: 10)
                Text(board.name)
                    .font(.title2.bold())
                    .foregroundStyle(Theme.textPrimary)
                Spacer()
                Button("Disconnect") {
                    boardManager.disconnect()
                }
                .font(.subheadline)
                .foregroundStyle(Theme.danger)
            }

            HStack(spacing: 24) {
                statItem(
                    label: "Battery",
                    value: "\(Int(boardManager.telemetry.batteryPercent))%",
                    color: Theme.batteryColor(boardManager.telemetry.batteryPercent)
                )
                statItem(
                    label: "Profile",
                    value: boardManager.activeProfile.name,
                    color: Theme.primary
                )
                statItem(
                    label: "State",
                    value: boardManager.refloatState.runState.description,
                    color: boardManager.refloatState.runState.color
                )
            }

            if let fw = board.refloatVersion {
                HStack {
                    Text("Refloat \(fw)")
                        .font(.caption)
                        .foregroundStyle(Theme.textTertiary)
                    Spacer()
                    Text("FW \(board.fwMajor).\(board.fwMinor)")
                        .font(.caption)
                        .foregroundStyle(Theme.textTertiary)
                }
            }
        }
        .card()
    }

    private func statItem(label: String, value: String, color: Color) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.title3.bold())
                .foregroundStyle(color)
            Text(label)
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)
        }
        .frame(maxWidth: .infinity)
    }

    // MARK: - Disconnected

    private var disconnectedCard: some View {
        VStack(spacing: 16) {
            Image(systemName: "wave.3.right")
                .font(.system(size: 48))
                .foregroundStyle(Theme.textTertiary)

            Text("No Board Connected")
                .font(.title3)
                .foregroundStyle(Theme.textSecondary)

            Button {
                if boardManager.isScanning {
                    boardManager.stopScan()
                } else {
                    boardManager.startScan()
                }
            } label: {
                HStack {
                    if boardManager.isScanning {
                        ProgressView()
                            .tint(.white)
                            .scaleEffect(0.8)
                        Text("Scanning...")
                    } else {
                        Image(systemName: "antenna.radiowaves.left.and.right")
                        Text("Scan for Boards")
                    }
                }
                .font(.headline)
                .foregroundStyle(.white)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
                .background(Theme.primary)
                .clipShape(RoundedRectangle(cornerRadius: 10))
            }

            if !boardManager.discoveredDevices.isEmpty {
                deviceList
            }
        }
        .card()
    }

    private var deviceList: some View {
        VStack(spacing: 0) {
            ForEach(boardManager.discoveredDevices) { device in
                Button {
                    boardManager.connect(to: device)
                } label: {
                    HStack {
                        Image(systemName: "dot.radiowaves.left.and.right")
                            .foregroundStyle(Theme.primary)
                        Text(device.name)
                            .foregroundStyle(Theme.textPrimary)
                        Spacer()
                        Text("\(device.rssi) dBm")
                            .font(.caption)
                            .foregroundStyle(Theme.textTertiary)
                        Image(systemName: "chevron.right")
                            .foregroundStyle(Theme.textTertiary)
                    }
                    .padding(.vertical, 10)
                }
                Divider().overlay(Theme.surfaceRaised)
            }
        }
    }

    // MARK: - Fleet

    private var fleetSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("My Boards")
                .font(.headline)
                .foregroundStyle(Theme.textPrimary)

            ForEach(boardManager.boards) { board in
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(board.name)
                            .font(.subheadline.bold())
                            .foregroundStyle(Theme.textPrimary)
                        if let hw = board.hwName {
                            Text(hw)
                                .font(.caption)
                                .foregroundStyle(Theme.textTertiary)
                        }
                    }
                    Spacer()
                    if let date = board.lastConnected {
                        Text(date, style: .relative)
                            .font(.caption)
                            .foregroundStyle(Theme.textTertiary)
                    }
                }
                .padding(.vertical, 4)
            }
        }
        .card()
    }
}
