import SwiftUI

struct RadarChartView: View {
    let values: [Double]
    let labels: [String]
    let accentColor: Color
    var maxValue: Double = 10.0
    var gridLevels: Int = 5

    var body: some View {
        GeometryReader { geo in
            let center = CGPoint(x: geo.size.width / 2, y: geo.size.height / 2)
            let radius = min(geo.size.width, geo.size.height) / 2 - 30

            ZStack {
                // Grid rings
                ForEach(1...gridLevels, id: \.self) { level in
                    let fraction = CGFloat(level) / CGFloat(gridLevels)
                    polygonPath(sides: values.count, center: center, radius: radius * fraction)
                        .stroke(Theme.surfaceRaised, lineWidth: 1)
                }

                // Axis lines
                ForEach(0..<values.count, id: \.self) { i in
                    let angle = angleForIndex(i, total: values.count)
                    Path { path in
                        path.move(to: center)
                        path.addLine(to: pointOnCircle(center: center, radius: radius, angle: angle))
                    }
                    .stroke(Theme.surfaceRaised, lineWidth: 1)
                }

                // Filled data shape
                dataPath(center: center, radius: radius)
                    .fill(accentColor.opacity(0.2))

                dataPath(center: center, radius: radius)
                    .stroke(accentColor, style: StrokeStyle(lineWidth: 2, lineJoin: .round))

                // Data points
                ForEach(0..<values.count, id: \.self) { i in
                    let angle = angleForIndex(i, total: values.count)
                    let r = radius * CGFloat(values[i] / maxValue)
                    let pt = pointOnCircle(center: center, radius: r, angle: angle)

                    Circle()
                        .fill(accentColor)
                        .frame(width: 8, height: 8)
                        .position(pt)
                }

                // Labels
                ForEach(0..<labels.count, id: \.self) { i in
                    let angle = angleForIndex(i, total: labels.count)
                    let pt = pointOnCircle(center: center, radius: radius + 20, angle: angle)

                    Text(labels[i])
                        .font(.caption2)
                        .foregroundStyle(Theme.textSecondary)
                        .position(pt)
                }
            }
        }
    }

    // MARK: - Geometry Helpers

    private func angleForIndex(_ index: Int, total: Int) -> Double {
        let slice = 2 * .pi / Double(total)
        return slice * Double(index) - .pi / 2 // Start from top
    }

    private func pointOnCircle(center: CGPoint, radius: CGFloat, angle: Double) -> CGPoint {
        CGPoint(
            x: center.x + radius * cos(angle),
            y: center.y + radius * sin(angle)
        )
    }

    private func polygonPath(sides: Int, center: CGPoint, radius: CGFloat) -> Path {
        Path { path in
            for i in 0...sides {
                let angle = angleForIndex(i % sides, total: sides)
                let pt = pointOnCircle(center: center, radius: radius, angle: angle)
                if i == 0 {
                    path.move(to: pt)
                } else {
                    path.addLine(to: pt)
                }
            }
            path.closeSubpath()
        }
    }

    private func dataPath(center: CGPoint, radius: CGFloat) -> Path {
        Path { path in
            for i in 0...values.count {
                let idx = i % values.count
                let angle = angleForIndex(idx, total: values.count)
                let r = radius * CGFloat(values[idx] / maxValue)
                let pt = pointOnCircle(center: center, radius: r, angle: angle)
                if i == 0 {
                    path.move(to: pt)
                } else {
                    path.addLine(to: pt)
                }
            }
            path.closeSubpath()
        }
    }
}
