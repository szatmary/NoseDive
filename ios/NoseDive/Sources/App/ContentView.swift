import SwiftUI

struct ContentView: View {
    @EnvironmentObject var boardManager: BoardManager

    var body: some View {
        TabView {
            HomeView()
                .tabItem {
                    Label("Home", systemImage: "house.fill")
                }

            DashboardView()
                .tabItem {
                    Label("Dash", systemImage: "gauge.open.with.lines.needle.33percent")
                }

            TuneView()
                .tabItem {
                    Label("Tune", systemImage: "slider.horizontal.3")
                }

            RidesView()
                .tabItem {
                    Label("Rides", systemImage: "map.fill")
                }

            ProfileView()
                .tabItem {
                    Label("Me", systemImage: "person.fill")
                }
        }
        .tint(Theme.primary)
    }
}
