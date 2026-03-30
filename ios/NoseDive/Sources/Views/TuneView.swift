import SwiftUI

struct TuneView: View {
    @EnvironmentObject var boardManager: BoardManager
    @State private var editingProfile: RiderProfile?
    @State private var showingPresets = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    radarSection
                    profilePicker
                    axisDetails
                    extraSliders
                }
                .padding()
            }
            .background(Theme.background)
            .navigationTitle("Tune")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button {
                        showingPresets = true
                    } label: {
                        Image(systemName: "square.grid.2x2")
                    }
                }
            }
            .sheet(isPresented: $showingPresets) {
                presetSheet
            }
        }
    }

    // MARK: - Radar Chart

    private var radarSection: some View {
        VStack(spacing: 8) {
            RadarChartView(
                values: boardManager.activeProfile.axisValues,
                labels: RiderProfile.axisLabels,
                accentColor: Theme.primary
            )
            .frame(height: 280)
            .padding(.horizontal)

            Text(boardManager.activeProfile.name)
                .font(.headline)
                .foregroundStyle(Theme.textPrimary)
        }
        .card()
    }

    // MARK: - Profile Picker

    private var profilePicker: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 10) {
                ForEach(boardManager.riderProfiles) { profile in
                    Button {
                        withAnimation(.easeInOut(duration: 0.3)) {
                            boardManager.selectProfile(profile)
                        }
                    } label: {
                        VStack(spacing: 6) {
                            Image(systemName: profile.icon)
                                .font(.title3)
                            Text(profile.name)
                                .font(.caption)
                        }
                        .foregroundStyle(
                            boardManager.activeProfile.id == profile.id
                                ? Theme.primary : Theme.textSecondary
                        )
                        .frame(width: 64, height: 64)
                        .background(
                            boardManager.activeProfile.id == profile.id
                                ? Theme.primarySoft : Theme.surfaceRaised
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                    }
                }
            }
            .padding(.horizontal, 4)
        }
    }

    // MARK: - Axis Details

    private var axisDetails: some View {
        VStack(spacing: 12) {
            ForEach(0..<6) { i in
                axisRow(
                    label: RiderProfile.axisLabels[i],
                    description: RiderProfile.axisDescriptions[i],
                    value: boardManager.activeProfile.axisValues[i]
                )
            }
        }
        .card()
    }

    private func axisRow(label: String, description: String, value: Double) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(label)
                    .font(.subheadline.bold())
                    .foregroundStyle(Theme.textPrimary)
                Spacer()
                Text(String(format: "%.0f", value))
                    .font(.subheadline.bold())
                    .foregroundStyle(Theme.primary)
            }
            Text(description)
                .font(.caption)
                .foregroundStyle(Theme.textTertiary)
            ProgressView(value: value, total: 10)
                .tint(Theme.primary)
        }
    }

    // MARK: - Extra Sliders

    private var extraSliders: some View {
        VStack(spacing: 12) {
            Text("Startup & Disengage")
                .font(.headline)
                .foregroundStyle(Theme.textPrimary)
                .frame(maxWidth: .infinity, alignment: .leading)

            VStack(spacing: 4) {
                HStack {
                    Text("Footpad Sensitivity")
                        .font(.subheadline)
                        .foregroundStyle(Theme.textSecondary)
                    Spacer()
                    Text(String(format: "%.0f", boardManager.activeProfile.footpadSensitivity))
                        .font(.subheadline.bold())
                        .foregroundStyle(Theme.primary)
                }
                ProgressView(value: boardManager.activeProfile.footpadSensitivity, total: 10)
                    .tint(Theme.primary)
            }

            VStack(spacing: 4) {
                HStack {
                    Text("Disengage Speed")
                        .font(.subheadline)
                        .foregroundStyle(Theme.textSecondary)
                    Spacer()
                    Text(String(format: "%.0f", boardManager.activeProfile.disengageSpeed))
                        .font(.subheadline.bold())
                        .foregroundStyle(Theme.primary)
                }
                ProgressView(value: boardManager.activeProfile.disengageSpeed, total: 10)
                    .tint(Theme.primary)
            }
        }
        .card()
    }

    // MARK: - Preset Sheet

    private var presetSheet: some View {
        NavigationStack {
            List {
                ForEach(RiderProfile.builtInPresets) { preset in
                    Button {
                        boardManager.selectProfile(preset)
                        showingPresets = false
                    } label: {
                        HStack(spacing: 12) {
                            Image(systemName: preset.icon)
                                .font(.title2)
                                .foregroundStyle(Theme.primary)
                                .frame(width: 40)
                            VStack(alignment: .leading) {
                                Text(preset.name)
                                    .font(.headline)
                                    .foregroundStyle(Theme.textPrimary)
                                Text(presetDescription(preset))
                                    .font(.caption)
                                    .foregroundStyle(Theme.textSecondary)
                            }
                            Spacer()
                            if boardManager.activeProfile.id == preset.id {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(Theme.primary)
                            }
                        }
                    }
                }
            }
            .navigationTitle("Presets")
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { showingPresets = false }
                }
            }
        }
        .presentationDetents([.medium])
    }

    private func presetDescription(_ profile: RiderProfile) -> String {
        switch profile.name {
        case "Chill": return "Relaxed cruising, maximum safety margins"
        case "Flow": return "Balanced all-around riding"
        case "Charge": return "Aggressive response for experienced riders"
        case "Trail": return "Nimble handling for off-road terrain"
        default: return ""
        }
    }
}
