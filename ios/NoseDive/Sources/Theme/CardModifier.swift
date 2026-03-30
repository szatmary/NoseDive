import SwiftUI

struct CardStyle: ViewModifier {
    var raised: Bool = false

    func body(content: Content) -> some View {
        content
            .padding(Theme.cardPadding)
            .background(raised ? Theme.surfaceRaised : Theme.surface)
            .clipShape(RoundedRectangle(cornerRadius: Theme.cardRadius))
    }
}

extension View {
    func card(raised: Bool = false) -> some View {
        modifier(CardStyle(raised: raised))
    }
}
