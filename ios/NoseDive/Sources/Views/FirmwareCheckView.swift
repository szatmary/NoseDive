import SwiftUI

struct FirmwareCheckView: View {
    @EnvironmentObject var boardManager: BoardManager
    let onComplete: () -> Void

    // Expected latest firmware versions (would come from a server in production)
    static let latestVESCFW = (major: UInt8(6), minor: UInt8(5))
    static let latestExpressFW = (major: UInt8(6), minor: UInt8(5))
    static let latestRefloat = (major: UInt8(2), minor: UInt8(0), patch: UInt8(1))

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                headerSection

                // Firmware checks in order: Express → VESC+Refloat → BMS
                expressSection
                vescSection
                refloatSection
                bmsSection

                // Continue button
                Button {
                    onComplete()
                } label: {
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
        .background(Theme.background)
        .navigationTitle("Firmware")
    }

    // MARK: - Header

    private var headerSection: some View {
        VStack(spacing: 8) {
            Image(systemName: "arrow.triangle.2.circlepath")
                .font(.system(size: 40))
                .foregroundStyle(Theme.primary)
            Text("Firmware Check")
                .font(.title3.bold())
                .foregroundStyle(Theme.textPrimary)
            Text("Checking firmware versions for all devices.")
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)
                .multilineTextAlignment(.center)
        }
        .padding(.vertical, 8)
    }

    // MARK: - VESC Express

    private var expressSection: some View {
        Group {
            if boardManager.canDevices.contains(253) {
                // Express FW not available via board callback — show as querying
                firmwareCard(
                    title: "VESC Express",
                    icon: "wifi",
                    step: 1,
                    currentFW: "Querying…",
                    latestFW: "\(Self.latestExpressFW.major).\(Self.latestExpressFW.minor)",
                    isUpToDate: nil,
                    detail: "ESP32"
                )
            }
        }
    }

    // MARK: - Main VESC

    private var vescSection: some View {
        Group {
            if let fw = boardManager.mainFWInfo {
                firmwareCard(
                    title: "VESC Motor Controller",
                    icon: "cpu",
                    step: 2,
                    currentFW: fw.versionString,
                    latestFW: "\(Self.latestVESCFW.major).\(Self.latestVESCFW.minor)",
                    isUpToDate: isFWUpToDate(fw, latest: Self.latestVESCFW),
                    detail: fw.hwName
                )
            }
        }
    }

    // MARK: - Refloat

    private var refloatSection: some View {
        Group {
            if let refloat = boardManager.refloatInfo {
                firmwareCard(
                    title: "Refloat Package",
                    icon: "figure.surfing",
                    step: 2,
                    currentFW: refloat.versionString,
                    latestFW: "\(Self.latestRefloat.major).\(Self.latestRefloat.minor).\(Self.latestRefloat.patch)",
                    isUpToDate: isRefloatUpToDate(refloat),
                    detail: refloat.name
                )
            } else if let fw = boardManager.mainFWInfo, fw.customConfigCount > 0 {
                firmwareCard(
                    title: "Refloat Package",
                    icon: "figure.surfing",
                    step: 2,
                    currentFW: "Querying…",
                    latestFW: "\(Self.latestRefloat.major).\(Self.latestRefloat.minor).\(Self.latestRefloat.patch)",
                    isUpToDate: nil,
                    detail: nil
                )
            } else {
                missingPackageCard
            }
        }
    }

    // MARK: - BMS

    private var bmsSection: some View {
        Group {
            if boardManager.canDevices.contains(10) {
                // BMS FW not available via board callback — show as querying
                firmwareCard(
                    title: "BMS",
                    icon: "battery.100.bolt",
                    step: 3,
                    currentFW: "Querying…",
                    latestFW: "\(Self.latestVESCFW.major).\(Self.latestVESCFW.minor)",
                    isUpToDate: nil,
                    detail: "VESC BMS"
                )
            }
        }
    }

    // MARK: - Card builders

    private func firmwareCard(
        title: String, icon: String, step: Int,
        currentFW: String, latestFW: String,
        isUpToDate: Bool?, detail: String?
    ) -> some View {
        VStack(spacing: 10) {
            HStack {
                Image(systemName: icon)
                    .font(.title3)
                    .foregroundStyle(Theme.primary)
                    .frame(width: 32)
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 4) {
                        Text("Step \(step)")
                            .font(.caption2.bold())
                            .foregroundStyle(Theme.textTertiary)
                        Text("·")
                            .foregroundStyle(Theme.textTertiary)
                        Text(title)
                            .font(.subheadline.bold())
                            .foregroundStyle(Theme.textPrimary)
                    }
                    if let detail {
                        Text(detail)
                            .font(.caption)
                            .foregroundStyle(Theme.textTertiary)
                    }
                }
                Spacer()
                statusBadge(isUpToDate)
            }

            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Installed")
                        .font(.caption2)
                        .foregroundStyle(Theme.textTertiary)
                    Text(currentFW)
                        .font(.subheadline.monospaced())
                        .foregroundStyle(Theme.textPrimary)
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 2) {
                    Text("Latest")
                        .font(.caption2)
                        .foregroundStyle(Theme.textTertiary)
                    Text(latestFW)
                        .font(.subheadline.monospaced())
                        .foregroundStyle(Theme.textPrimary)
                }
            }

            if isUpToDate == false {
                Button {
                    // TODO: firmware update flow
                } label: {
                    Text("Update Available")
                        .font(.caption.bold())
                        .foregroundStyle(.white)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 8)
                        .background(Theme.warning)
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                }
            }
        }
        .card()
    }

    private var missingPackageCard: some View {
        VStack(spacing: 10) {
            HStack {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundStyle(Theme.warning)
                Text("No Refloat Package")
                    .font(.subheadline.bold())
                    .foregroundStyle(Theme.textPrimary)
                Spacer()
            }
            Text("Refloat is required for onewheel balancing. Install it to continue riding.")
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)

            Button {
                boardManager.installRefloat()
            } label: {
                Text("Install Refloat")
                    .font(.caption.bold())
                    .foregroundStyle(.white)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
                    .background(Theme.primary)
                    .clipShape(RoundedRectangle(cornerRadius: 8))
            }
        }
        .card()
    }

    private func statusBadge(_ isUpToDate: Bool?) -> some View {
        Group {
            if let upToDate = isUpToDate {
                if upToDate {
                    Label("Current", systemImage: "checkmark.circle.fill")
                        .font(.caption)
                        .foregroundStyle(Theme.success)
                } else {
                    Label("Update", systemImage: "arrow.down.circle.fill")
                        .font(.caption)
                        .foregroundStyle(Theme.warning)
                }
            } else {
                ProgressView()
                    .scaleEffect(0.7)
            }
        }
    }

    // MARK: - Version checks

    private func isFWUpToDate(_ fw: FWVersionInfo, latest: (major: UInt8, minor: UInt8)) -> Bool {
        fw.major > latest.major ||
            (fw.major == latest.major && fw.minor >= latest.minor)
    }

    private func isRefloatUpToDate(_ info: RefloatInfo) -> Bool {
        if info.major > Self.latestRefloat.major { return true }
        if info.major < Self.latestRefloat.major { return false }
        if info.minor > Self.latestRefloat.minor { return true }
        if info.minor < Self.latestRefloat.minor { return false }
        return info.patch >= Self.latestRefloat.patch
    }
}
