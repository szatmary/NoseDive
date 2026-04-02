import SwiftUI

// MARK: - Wizard Step Protocol

/// Each wizard step is identified by a unique ID.
/// To add, remove, or reorder steps, edit `WizardStepID.defaultSteps`.
enum WizardStepID: String, CaseIterable, Identifiable {
    case identify
    case refloatCheck
    case firmwareCheck
    case complete

    var id: String { rawValue }

    /// The default ordered list of steps for board setup.
    /// Reorder this array or remove entries to change the wizard flow.
    static let defaultSteps: [WizardStepID] = [
        .firmwareCheck,
        .identify,
        .refloatCheck,
        .complete,
    ]

    var title: String {
        switch self {
        case .identify: return "Identify"
        case .refloatCheck: return "Refloat"
        case .firmwareCheck: return "Firmware"
        case .complete: return "Complete"
        }
    }
}

// MARK: - Wizard View

struct SetupWizardView: View {
    @EnvironmentObject var boardManager: BoardManager
    @Binding var isPresented: Bool

    /// The ordered steps for this wizard run. Modify to customize flow.
    @State private var steps: [WizardStepID]
    @State private var currentIndex: Int = 0

    init(isPresented: Binding<Bool>, steps: [WizardStepID] = WizardStepID.defaultSteps) {
        _isPresented = isPresented
        _steps = State(initialValue: steps)
    }

    private var currentStep: WizardStepID {
        steps[currentIndex]
    }

    private var isLastStep: Bool {
        currentIndex >= steps.count - 1
    }

    private func advance() {
        if isLastStep { return }
        withAnimation { currentIndex += 1 }
    }

    private func goBack() {
        if currentIndex > 0 {
            withAnimation { currentIndex -= 1 }
        }
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Progress indicator
                if steps.count > 2 {
                    stepIndicator
                        .padding(.horizontal)
                        .padding(.top, 8)
                }

                // Step content
                Group {
                    switch currentStep {
                    case .identify:
                        IdentifyStepView(onContinue: advance, onSkip: {
                            saveBoard()
                            isPresented = false
                        })
                    case .refloatCheck:
                        RefloatCheckStepView(onContinue: advance)
                    case .firmwareCheck:
                        FirmwareCheckView(onComplete: advance)
                    case .complete:
                        CompleteStepView(onDone: {
                            saveBoard()
                            isPresented = false
                        })
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .background(Theme.background)
            .navigationTitle("Board Setup")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    if currentIndex > 0 && currentStep != .complete {
                        Button("Back") { goBack() }
                    } else {
                        Button("Cancel") { isPresented = false }
                    }
                }
            }
        }
    }

    // MARK: - Step Indicator

    private var stepIndicator: some View {
        HStack(spacing: 4) {
            ForEach(Array(steps.enumerated()), id: \.element) { index, step in
                VStack(spacing: 4) {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(index <= currentIndex ? Theme.primary : Theme.surfaceRaised)
                        .frame(height: 3)
                    Text(step.title)
                        .font(.system(size: 9, weight: index == currentIndex ? .bold : .regular))
                        .foregroundStyle(index <= currentIndex ? Theme.primary : Theme.textTertiary)
                }
            }
        }
    }

    // MARK: - Save

    private func saveBoard() {
        if var board = boardManager.activeBoard {
            board.wizardComplete = true
            boardManager.activeBoard = board
            if !boardManager.boards.contains(where: { $0.id == board.id }) {
                boardManager.boards.append(board)
            } else if let idx = boardManager.boards.firstIndex(where: { $0.id == board.id }) {
                boardManager.boards[idx] = board
            }
            boardManager.saveToDisk()
        }
    }
}

// MARK: - Identify Step

struct IdentifyStepView: View {
    @EnvironmentObject var boardManager: BoardManager
    let onContinue: () -> Void
    let onSkip: () -> Void

    var body: some View {
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
                if let fw = boardManager.mainFWInfo {
                    VStack(spacing: 12) {
                        infoRow("Hardware", fw.hwName)
                        infoRow("Firmware", fw.versionString)
                        infoRow("UUID", String(fw.uuid.prefix(16)) + "…")

                        if let refloat = boardManager.refloatInfo {
                            infoRow("Package", "\(refloat.name) \(refloat.versionString)")
                        } else if fw.customConfigCount > 0 {
                            infoRow("Package", "Detected (querying…)")
                        } else {
                            infoRow("Package", "None installed")
                                .foregroundStyle(Theme.warning)
                        }
                    }
                    .card()
                }

                // (board type guessing moved to engine — TODO: expose via callback)

                // CAN bus devices
                if !boardManager.canDevices.isEmpty {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("CAN Bus Devices")
                            .font(.headline)
                            .foregroundStyle(Theme.textPrimary)

                        ForEach(boardManager.canDevices, id: \.self) { id in
                            HStack {
                                Image(systemName: iconForDevice(id))
                                    .foregroundStyle(Theme.primary)
                                    .frame(width: 24)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(nameForDevice(id))
                                        .font(.subheadline)
                                        .foregroundStyle(Theme.textPrimary)
                                    // Main VESC FW version shown for CAN ID 0
                                    if id == 0, let fw = boardManager.mainFWInfo {
                                        Text("FW \(fw.versionString)")
                                            .font(.caption)
                                            .foregroundStyle(Theme.textTertiary)
                                    }
                                }
                                Spacer()
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(Theme.success)
                            }
                        }
                    }
                    .card()
                }

                // Action buttons
                VStack(spacing: 12) {
                    Button(action: onContinue) {
                        Text("Continue Setup")
                            .font(.headline)
                            .foregroundStyle(.white)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 12)
                            .background(Theme.primary)
                            .clipShape(RoundedRectangle(cornerRadius: 10))
                    }

                    Button(action: onSkip) {
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

// MARK: - Refloat Check Step

struct RefloatCheckStepView: View {
    @EnvironmentObject var boardManager: BoardManager
    let onContinue: () -> Void

    var body: some View {
        ScrollView {
            VStack(spacing: 20) {
                Image(systemName: "figure.surfing")
                    .font(.system(size: 56))
                    .foregroundStyle(Theme.primary)
                    .padding(.top, 24)

                Text("Refloat Package")
                    .font(.title2.bold())
                    .foregroundStyle(Theme.textPrimary)

                Text("Refloat is the balance package that makes your board rideable.")
                    .font(.subheadline)
                    .foregroundStyle(Theme.textSecondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)

                if boardManager.hasRefloat {
                    installedCard
                } else if boardManager.refloatInstalling {
                    installingCard
                } else if boardManager.refloatInstalled {
                    justInstalledCard
                } else {
                    notInstalledCard
                }

                Button(action: onContinue) {
                    Text("Continue")
                        .font(.headline)
                        .foregroundStyle(.white)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 12)
                        .background(Theme.primary)
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                }
                .padding(.top, 8)
            }
            .padding()
        }
    }

    // MARK: - State cards

    private var installedCard: some View {
        VStack(spacing: 12) {
            HStack {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(Theme.success)
                    .font(.title3)
                Text("Refloat Installed")
                    .font(.headline)
                    .foregroundStyle(Theme.textPrimary)
                Spacer()
            }

            if let info = boardManager.refloatInfo {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Version")
                            .font(.caption)
                            .foregroundStyle(Theme.textTertiary)
                        Text(info.versionString)
                            .font(.subheadline.monospaced())
                            .foregroundStyle(Theme.textPrimary)
                    }
                    Spacer()
                    VStack(alignment: .trailing, spacing: 2) {
                        Text("Package")
                            .font(.caption)
                            .foregroundStyle(Theme.textTertiary)
                        Text(info.name)
                            .font(.subheadline)
                            .foregroundStyle(Theme.textPrimary)
                    }
                }
            } else {
                HStack {
                    ProgressView()
                        .scaleEffect(0.7)
                    Text("Querying version…")
                        .font(.caption)
                        .foregroundStyle(Theme.textSecondary)
                }
            }
        }
        .card()
    }

    private var notInstalledCard: some View {
        VStack(spacing: 12) {
            HStack {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundStyle(Theme.warning)
                    .font(.title3)
                Text("Refloat Not Installed")
                    .font(.headline)
                    .foregroundStyle(Theme.textPrimary)
                Spacer()
            }

            Text("Your board does not have Refloat installed. Without it, the board cannot balance. Install Refloat to make your board rideable.")
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)

            Button {
                boardManager.installRefloat()
            } label: {
                Label("Install Refloat", systemImage: "arrow.down.circle.fill")
                    .font(.headline)
                    .foregroundStyle(.white)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 12)
                    .background(Theme.primary)
                    .clipShape(RoundedRectangle(cornerRadius: 10))
            }
        }
        .card()
    }

    private var installingCard: some View {
        VStack(spacing: 16) {
            ProgressView()
                .scaleEffect(1.2)
            Text("Installing Refloat…")
                .font(.headline)
                .foregroundStyle(Theme.textPrimary)
            Text("Do not disconnect your board during installation.")
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .card()
    }

    private var justInstalledCard: some View {
        VStack(spacing: 12) {
            HStack {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(Theme.success)
                    .font(.title3)
                Text("Refloat Installed Successfully")
                    .font(.headline)
                    .foregroundStyle(Theme.textPrimary)
                Spacer()
            }

            if let info = boardManager.refloatInfo {
                HStack {
                    Text("Version \(info.versionString)")
                        .font(.subheadline.monospaced())
                        .foregroundStyle(Theme.textPrimary)
                    Spacer()
                }
            }
        }
        .card()
    }
}

// MARK: - Complete Step

struct CompleteStepView: View {
    @EnvironmentObject var boardManager: BoardManager
    let onDone: () -> Void

    var body: some View {
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

            Button(action: onDone) {
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
}
