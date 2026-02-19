import SwiftUI
import WatchKit

/// Main score control view for Apple Watch
/// Uses Digital Crown rotation to increment scores:
/// - Crown scroll UP (positive) → increment Red
/// - Crown scroll DOWN (negative) → increment Blue
struct ScoreControlView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel
    @FocusState private var isFocused: Bool
    @State private var crownRotation: Double = 0.0
    @State private var accumulatedRotation: Double = 0.0
    @State private var lastScoreTime: Date = .distantPast
    @State private var showingSettings = false
    @State private var showingResetConfirm = false

    var body: some View {
        GeometryReader { geometry in
            HStack(spacing: 4) {
                // Blue team - left column
                scorePanel(
                    score: viewModel.state.blueScore,
                    color: .blue,
                    label: "BLUE",
                    height: geometry.size.height
                )

                // Red team - right column
                scorePanel(
                    score: viewModel.state.redScore,
                    color: .red,
                    label: "RED",
                    height: geometry.size.height
                )
            }
            .padding(.horizontal, 2)
        }
        .focusable()
        .focused($isFocused)
        .digitalCrownRotation(
            $crownRotation,
            from: -100.0,
            through: 100.0,
            sensitivity: .low,
            isContinuous: true,
            isHapticFeedbackEnabled: false
        )
        .onChange(of: crownRotation) { oldValue, newValue in
            handleCrownRotation(oldValue: oldValue, newValue: newValue)
        }
        .onAppear {
            isFocused = true
        }
        .onChange(of: showingSettings) { _, isShowing in
            if !isShowing { isFocused = true }
        }
        .onChange(of: showingResetConfirm) { _, isShowing in
            if !isShowing { isFocused = true }
        }
        .confirmationDialog("Options", isPresented: $showingResetConfirm) {
            Button("Reset to 00-00", role: .destructive) {
                viewModel.resetScores()
            }
            Button("Blue -1") {
                viewModel.decrementBlueScore()
            }
            Button("Red -1") {
                viewModel.decrementRedScore()
            }
            Button("Disconnect", role: .destructive) {
                viewModel.bleManager.disconnect()
            }
            Button("Cancel", role: .cancel) {}
        }
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    showingSettings = true
                } label: {
                    Image(systemName: "gearshape")
                }
            }
        }
        .sheet(isPresented: $showingSettings) {
            SettingsView()
        }
    }

    // MARK: - Score Panel

    private func scorePanel(
        score: UInt8,
        color: Color,
        label: String,
        height: CGFloat
    ) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12)
                .fill(color.opacity(0.3))

            VStack(spacing: 2) {
                Text(String(format: "%02d", score))
                    .font(.system(size: 42, weight: .bold, design: .monospaced))
                    .foregroundColor(.white)
                Text(label)
                    .font(.caption2)
                    .fontWeight(.semibold)
                    .foregroundColor(color)
            }
        }
        .frame(maxWidth: .infinity)
        .frame(height: height * 0.85)
        .contentShape(Rectangle())
        .onLongPressGesture(minimumDuration: 0.5) {
            showingResetConfirm = true
        }
    }

    // MARK: - Crown Rotation Handling

    private func handleCrownRotation(oldValue: Double, newValue: Double) {
        let delta = newValue - oldValue
        // Guard against wrap-around artifacts in continuous mode
        guard abs(delta) < 10.0 else { return }

        // Require a pause after each score before allowing another
        let cooldown: TimeInterval = 0.5
        if Date().timeIntervalSince(lastScoreTime) < cooldown {
            accumulatedRotation = 0
            return
        }

        accumulatedRotation += delta
        let threshold = Constants.DigitalCrown.scoreThreshold

        if accumulatedRotation >= threshold {
            viewModel.incrementRedScore()
            accumulatedRotation = 0
            lastScoreTime = Date()
            WKInterfaceDevice.current().play(.notification)
        } else if accumulatedRotation <= -threshold {
            viewModel.incrementBlueScore()
            accumulatedRotation = 0
            lastScoreTime = Date()
            WKInterfaceDevice.current().play(.notification)
        }
    }
}
