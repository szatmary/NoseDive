import SwiftUI

@main
struct NoseDiveApp: App {
    @StateObject private var boardManager = BoardManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(boardManager)
                .preferredColorScheme(.dark)
        }
    }
}
