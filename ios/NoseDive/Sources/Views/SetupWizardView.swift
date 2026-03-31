import SwiftUI

struct SetupWizardView: View {
    @EnvironmentObject var boardManager: BoardManager
    @Binding var isPresented: Bool
    @State private var step: WizardStep = .identify

    enum WizardStep {
        case identify
        case firmwareCheck
        case complete
    }

    var body: some View {
        NavigationStack {
            Group {
                switch step {
                case .identify:
                    identifyStep
                case .firmwareCheck:
                    FirmwareCheckView(onComplete: {
                        step = .complete
                    })
                case .complete:
                    completeStep
                }
            }
            .background(Theme.background)
            .navigationTitle("Board Setup")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Cancel") { isPresented = false }
                }
            }
        }
    }

    // MARK: - Identify

    private var identifyStep: some View {
        ScrollView {
            VStack(spacing: 20) {
                Image(systemName: "cpu")
                    .font(.system(size: 56))
                    .foregroundStyle(Theme.primary)
                    .padding(.top, 24)

                Text("New Board Detected")
                    .font(.title2.bold())
                    .foregroundStyle(Theme.textPrimary)

                // Board info card
                if let mainFW = boardManager.deviceFWInfo[0]?.fwInfo {
                    VStack(spacing: 12) {
                        infoRow("Hardware", mainFW.hwName)
                        infoRow("Firmware", "\(mainFW.major).\(mainFW.minor)")
                        infoRow("UUID", String(mainFW.uuid.prefix(16)) + "…")

                        if let refloat = boardManager.refloatInfo {
                            infoRow("Package", "\(refloat.name) \(refloat.versionString)")
                        } else if mainFW.customConfigCount > 0 {
                            infoRow("Package", "Detected (querying…)")
                        } else {
                            infoRow("Package", "None installed")
                                .foregroundStyle(Theme.warning)
                        }
                    }
                    .card()
                }

                // Guessed board type
                if let guess = boardManager.guessedBoardType {
                    HStack {
                        Image(systemName: "sparkles")
                            .foregroundStyle(Theme.primary)
                        Text("Looks like a **\(guess)**")
                            .foregroundStyle(Theme.textPrimary)
                    }
                    .card()
                }

                // CAN bus devices
                if !boardManager.canDevices.isEmpty {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("CAN Bus Devices")
                            .font(.headline)
                            .foregroundStyle(Theme.textPrimary)

                        ForEach(boardManager.canDevices, id: \.self) { id in
                            let dev = boardManager.deviceFWInfo[id]
                            HStack {
                                Image(systemName: iconForDevice(id))
                                    .foregroundStyle(Theme.primary)
                                    .frame(width: 24)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(dev?.displayName ?? nameForDevice(id))
                                        .font(.subheadline)
                                        .foregroundStyle(Theme.textPrimary)
                                    if let fw = dev?.fwInfo {
                                        Text("FW \(fw.major).\(fw.minor)")
                                            .font(.caption)
                                            .foregroundStyle(Theme.textTertiary)
                                    }
                                }
                                Spacer()
                                if dev?.fwInfo != nil {
                                    Image(systemName: "checkmark.circle.fill")
                                        .foregroundStyle(Theme.success)
                                } else {
                                    ProgressView()
                                        .scaleEffect(0.7)
                                }
                            }
                        }
                    }
                    .card()
                }

                // Action buttons
                VStack(spacing: 12) {
                    Button {
                        step = .firmwareCheck
                    } label: {
                        Text("Check Firmware")
                            .font(.headline)
                            .foregroundStyle(.white)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 12)
                            .background(Theme.primary)
                            .clipShape(RoundedRectangle(cornerRadius: 10))
                    }

                    Button {
                        // Save board as-is and skip wizard
                        saveBoard()
                        isPresented = false
                    } label: {
                        Text("Skip Setup")
                            .font(.subheadline)
                            .foregroundStyle(Theme.textSecondary)
                    }
                }
                .padding(.top, 8)
            }
            .padding()
        }
    }

    // MARK: - Complete

    private var completeStep: some View {
        VStack(spacing: 24) {
            Spacer()

            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: 72))
                .foregroundStyle(Theme.success)

            Text("Setup Complete")
                .font(.title2.bold())
                .foregroundStyle(Theme.textPrimary)

            if let board = boardManager.activeBoard {
                Text("**\(board.name)** is ready to ride.")
                    .foregroundStyle(Theme.textSecondary)
            }

            Spacer()

            Button {
                saveBoard()
                isPresented = false
            } label: {
                Text("Done")
                    .font(.headline)
                    .foregroundStyle(.white)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 12)
                    .background(Theme.primary)
                    .clipShape(RoundedRectangle(cornerRadius: 10))
            }
            .padding()
        }
    }

    // MARK: - Helpers

    private func saveBoard() {
        if var board = boardManager.activeBoard {
            board.wizardComplete = true
            boardManager.activeBoard = board
            if !boardManager.boards.contains(where: { $0.id == board.id }) {
                boardManager.boards.append(board)
            }
        }
    }

    private func infoRow(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label)
                .font(.subheadline)
                .foregroundStyle(Theme.textSecondary)
            Spacer()
            Text(value)
                .font(.subheadline.monospaced())
                .foregroundStyle(Theme.textPrimary)
        }
    }

    private func iconForDevice(_ id: UInt8) -> String {
        switch id {
        case 0: return "cpu"
        case 10: return "battery.100.bolt"
        case 253: return "wifi"
        default: return "circle.dotted"
        }
    }

    private func nameForDevice(_ id: UInt8) -> String {
        switch id {
        case 0: return "VESC Motor Controller"
        case 10: return "BMS"
        case 253: return "VESC Express"
        default: return "Device \(id)"
        }
    }
}
