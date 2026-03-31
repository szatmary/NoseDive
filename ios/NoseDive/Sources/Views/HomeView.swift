import SwiftUI

struct HomeView: View {
    @EnvironmentObject var boardManager: BoardManager
    @State private var showTCPSheet = false
    @State private var tcpHost = "127.0.0.1"
    @State private var tcpPort = "65102"

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if boardManager.isConnected, let board = boardManager.activeBoard {
                        connectedCard(board)
                        canBusCard
                    } else if boardManager.isConnected {
                        connectedMinimalCard
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
            .sheet(isPresented: $showTCPSheet) {
                tcpConnectSheet
            }
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
            } else {
                HStack {
                    Text("FW \(board.fwMajor).\(board.fwMinor)")
                        .font(.caption)
                        .foregroundStyle(Theme.textTertiary)
                    if let hw = board.hwName {
                        Spacer()
                        Text(hw)
                            .font(.caption)
                            .foregroundStyle(Theme.textTertiary)
                    }
                }
            }
        }
        .card()
    }

    private var connectedMinimalCard: some View {
        VStack(spacing: 12) {
            HStack {
                Circle()
                    .fill(Theme.success)
                    .frame(width: 10, height: 10)
                Text("Connected")
                    .font(.title2.bold())
                    .foregroundStyle(Theme.textPrimary)
                Spacer()
                Button("Disconnect") {
                    boardManager.disconnect()
                }
                .font(.subheadline)
                .foregroundStyle(Theme.danger)
            }
        }
        .card()
    }

    // MARK: - CAN Bus

    private var canBusCard: some View {
        Group {
            if !boardManager.canDevices.isEmpty {
                VStack(alignment: .leading, spacing: 8) {
                    Text("CAN Bus")
                        .font(.headline)
                        .foregroundStyle(Theme.textPrimary)
                    ForEach(boardManager.canDevices, id: \.self) { id in
                        HStack {
                            Image(systemName: iconForCANDevice(id))
                                .foregroundStyle(Theme.primary)
                                .frame(width: 24)
                            Text(nameForCANDevice(id))
                                .font(.subheadline)
                                .foregroundStyle(Theme.textPrimary)
                            Spacer()
                            Text("ID \(id)")
                                .font(.caption.monospaced())
                                .foregroundStyle(Theme.textTertiary)
                        }
                        .padding(.vertical, 2)
                    }
                }
                .card()
            }
        }
    }

    private func nameForCANDevice(_ id: UInt8) -> String {
        switch id {
        case 0: return "VESC Motor Controller"
        case 10: return "BMS"
        case 253: return "VESC Express"
        default: return "Device \(id)"
        }
    }

    private func iconForCANDevice(_ id: UInt8) -> String {
        switch id {
        case 0: return "cpu"
        case 10: return "battery.100.bolt"
        case 253: return "wifi"
        default: return "circle.dotted"
        }
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

            // BLE scan button
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

            // TCP connect button
            Button {
                showTCPSheet = true
            } label: {
                HStack {
                    Image(systemName: "network")
                    Text("Connect via TCP")
                }
                .font(.subheadline)
                .foregroundStyle(Theme.primary)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
                .background(Theme.primarySoft)
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

    // MARK: - TCP Connect Sheet

    private var tcpConnectSheet: some View {
        NavigationStack {
            VStack(spacing: 20) {
                Text("Connect to VESC over TCP")
                    .font(.headline)
                    .foregroundStyle(Theme.textPrimary)

                Text("Enter the address of a VESC simulator or TCP-bridged board.")
                    .font(.caption)
                    .foregroundStyle(Theme.textSecondary)
                    .multilineTextAlignment(.center)

                VStack(spacing: 12) {
                    HStack {
                        Text("Host")
                            .font(.subheadline)
                            .foregroundStyle(Theme.textSecondary)
                            .frame(width: 50, alignment: .leading)
                        TextField("127.0.0.1", text: $tcpHost)
                            .textFieldStyle(.roundedBorder)
                            .autocorrectionDisabled()
                            #if os(iOS)
                            .keyboardType(.decimalPad)
                            #endif
                    }
                    HStack {
                        Text("Port")
                            .font(.subheadline)
                            .foregroundStyle(Theme.textSecondary)
                            .frame(width: 50, alignment: .leading)
                        TextField("65102", text: $tcpPort)
                            .textFieldStyle(.roundedBorder)
                            #if os(iOS)
                            .keyboardType(.numberPad)
                            #endif
                    }
                }

                Button {
                    let port = UInt16(tcpPort) ?? 65102
                    boardManager.connectTCP(host: tcpHost, port: port)
                    showTCPSheet = false
                } label: {
                    Text("Connect")
                        .font(.headline)
                        .foregroundStyle(.white)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 12)
                        .background(Theme.primary)
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                }

                Spacer()
            }
            .padding()
            .navigationTitle("TCP Connection")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Cancel") { showTCPSheet = false }
                }
            }
        }
        .presentationDetents([.medium])
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
