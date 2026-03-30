import SwiftUI

struct RidesView: View {
    @EnvironmentObject var boardManager: BoardManager

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    if boardManager.isConnected {
                        recordCard
                    }

                    statsCard

                    emptyState
                }
                .padding()
            }
            .background(Theme.background)
            .navigationTitle("Rides")
        }
    }

    // MARK: - Record

    private var recordCard: some View {
        VStack(spacing: 12) {
            HStack {
                Image(systemName: "record.circle")
                    .font(.title2)
                    .foregroundStyle(Theme.danger)
                VStack(alignment: .leading, spacing: 2) {
                    Text("Ready to Record")
                        .font(.subheadline.bold())
                        .foregroundStyle(Theme.textPrimary)
                    Text("GPS ride logging with telemetry overlay")
                        .font(.caption)
                        .foregroundStyle(Theme.textSecondary)
                }
                Spacer()
            }

            Button {
                // TODO: Start ride recording
            } label: {
                Text("Start Ride")
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

    // MARK: - Stats

    private var statsCard: some View {
        VStack(spacing: 12) {
            Text("Lifetime Stats")
                .font(.headline)
                .foregroundStyle(Theme.textPrimary)
                .frame(maxWidth: .infinity, alignment: .leading)

            HStack(spacing: 16) {
                statBox("Distance",
                        String(format: "%.1f mi", (boardManager.activeBoard?.lifetimeDistanceM ?? 0) * 0.000621371))
                statBox("Rides", "\(boardManager.activeBoard?.rideCount ?? 0)")
            }
        }
        .card()
    }

    private func statBox(_ label: String, _ value: String) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.title2.bold())
                .foregroundStyle(Theme.primary)
            Text(label)
                .font(.caption)
                .foregroundStyle(Theme.textSecondary)
        }
        .frame(maxWidth: .infinity)
    }

    // MARK: - Empty

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(systemName: "map")
                .font(.system(size: 48))
                .foregroundStyle(Theme.textTertiary)
            Text("No rides yet")
                .font(.subheadline)
                .foregroundStyle(Theme.textSecondary)
            Text("Connect your board and start recording to see your ride history here.")
                .font(.caption)
                .foregroundStyle(Theme.textTertiary)
                .multilineTextAlignment(.center)
        }
        .padding(.vertical, 32)
    }
}
