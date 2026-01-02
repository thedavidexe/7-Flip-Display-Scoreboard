import SwiftUI

/// Score display and controls for a single team
struct TeamScoreView: View {
    let teamName: String
    let teamColor: Color
    let score: UInt8
    let increment: Int
    let onIncrement: () -> Void
    let onDecrement: () -> Void
    let onScoreSet: (UInt8) -> Void

    @State private var showScoreInput = false
    @State private var inputScore = ""

    var body: some View {
        VStack(spacing: 16) {
            // Team name
            Text(teamName)
                .font(.headline)
                .foregroundColor(teamColor)

            // Score display (tappable)
            Button(action: {
                inputScore = String(score)
                showScoreInput = true
            }) {
                Text(String(format: "%02d", score))
                    .font(.system(size: 72, weight: .bold, design: .monospaced))
                    .foregroundColor(teamColor)
                    .minimumScaleFactor(0.5)
            }
            .buttonStyle(.plain)

            // Increment button (large)
            Button(action: onIncrement) {
                Text("+\(increment)")
                    .font(.system(size: 32, weight: .bold))
                    .frame(maxWidth: .infinity)
                    .frame(height: 80)
            }
            .buttonStyle(.borderedProminent)
            .tint(teamColor)

            // Decrement button (smaller)
            Button(action: onDecrement) {
                Text("-1")
                    .font(.title2)
                    .frame(maxWidth: .infinity)
                    .frame(height: 44)
            }
            .buttonStyle(.bordered)
            .disabled(score == 0)
        }
        .padding()
        .frame(maxWidth: .infinity)
        .alert("Set \(teamName) Score", isPresented: $showScoreInput) {
            TextField("Score (0-99)", text: $inputScore)
                .keyboardType(.numberPad)

            Button("Cancel", role: .cancel) {
                inputScore = ""
            }

            Button("Set") {
                if let value = UInt8(inputScore), value <= 99 {
                    onScoreSet(value)
                }
                inputScore = ""
            }
        } message: {
            Text("Enter a score between 0 and 99")
        }
    }
}
