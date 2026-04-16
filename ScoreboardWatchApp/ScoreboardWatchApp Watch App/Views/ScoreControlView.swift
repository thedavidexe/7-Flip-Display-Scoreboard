import SwiftUI
import WatchKit
import HealthKit

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
    @State private var idleResetTask: Task<Void, Never>?
    @State private var showingSettings = false
    @State private var showingResetConfirm = false
    @State private var runtimeManager = ExtendedRuntimeManager()

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
        .overlay(alignment: .bottom) {
            if viewModel.connectionStatus == .connecting {
                Text("Reconnecting...")
                    .font(.system(size: 11, weight: .medium))
                    .foregroundColor(.orange)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 3)
                    .background(.black.opacity(0.6), in: Capsule())
                    .padding(.bottom, 4)
            }
        }
        .focusable()
        .focused($isFocused)
        .digitalCrownRotation(
            $crownRotation,
            from: -100.0,
            through: 100.0,
            sensitivity: .low,
            isContinuous: true,
            isHapticFeedbackEnabled: true
        )
        .onChange(of: crownRotation) { oldValue, newValue in
            handleCrownRotation(oldValue: oldValue, newValue: newValue)
        }
        .onAppear {
            isFocused = true
            runtimeManager.start()
        }
        .onDisappear {
            runtimeManager.stop()
        }
        .onChange(of: showingSettings) { _, isShowing in
            if !isShowing { isFocused = true }
        }
        .onChange(of: showingResetConfirm) { _, isShowing in
            if !isShowing { isFocused = true }
        }
        .confirmationDialog("Options", isPresented: $showingResetConfirm) {
            Button("Refresh Display") {
                viewModel.sendCurrentState(forceUpdate: true)
            }
            Button("Blue -1") {
                viewModel.decrementBlueScore()
            }
            Button("Red -1") {
                viewModel.decrementRedScore()
            }
            Button("Reset to 00-00", role: .destructive) {
                viewModel.resetScores()
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
            idleResetTask?.cancel()
            // Only give success haptic when actually connected; suppress during reconnect
            if viewModel.isConnected {
                WKInterfaceDevice.current().play(.success)
            }
        } else if accumulatedRotation <= -threshold {
            viewModel.incrementBlueScore()
            accumulatedRotation = 0
            lastScoreTime = Date()
            idleResetTask?.cancel()
            if viewModel.isConnected {
                WKInterfaceDevice.current().play(.success)
            }
        } else {
            scheduleIdleReset()
        }
    }

    /// Reset the accumulator if the crown stops moving
    private func scheduleIdleReset() {
        idleResetTask?.cancel()
        idleResetTask = Task {
            try? await Task.sleep(nanoseconds: 500_000_000) // 0.5s idle
            if !Task.isCancelled {
                accumulatedRotation = 0
            }
        }
    }
}

// MARK: - Workout Session Manager
//
// HKWorkoutSession keeps the app in the active foreground state, which allows
// the Digital Crown to deliver events even when the wrist is lowered and the
// display is off. WKExtendedRuntimeSession alone does not guarantee this.

private class ExtendedRuntimeManager: NSObject, HKWorkoutSessionDelegate, HKLiveWorkoutBuilderDelegate {
    private let healthStore = HKHealthStore()
    private var workoutSession: HKWorkoutSession?

    func start() {
        guard HKHealthStore.isHealthDataAvailable() else { return }
        // Request authorization (no-op if already granted)
        healthStore.requestAuthorization(toShare: [HKQuantityType.workoutType()], read: []) { [weak self] granted, _ in
            guard granted else { return }
            DispatchQueue.main.async { self?.beginSession() }
        }
    }

    private func beginSession() {
        guard workoutSession == nil else { return }
        let config = HKWorkoutConfiguration()
        config.activityType = .other
        config.locationType = .unknown
        do {
            let session = try HKWorkoutSession(healthStore: healthStore, configuration: config)
            session.delegate = self
            workoutSession = session
            session.startActivity(with: Date())
        } catch {}
    }

    func stop() {
        workoutSession?.end()
        workoutSession = nil
    }

    // MARK: HKWorkoutSessionDelegate
    func workoutSession(_ workoutSession: HKWorkoutSession,
                        didChangeTo toState: HKWorkoutSessionState,
                        from fromState: HKWorkoutSessionState,
                        date: Date) {}

    func workoutSession(_ workoutSession: HKWorkoutSession, didFailWithError error: Error) {}

    // MARK: HKLiveWorkoutBuilderDelegate (required by protocol, unused)
    func workoutBuilderDidCollectEvent(_ workoutBuilder: HKLiveWorkoutBuilder) {}
    func workoutBuilder(_ workoutBuilder: HKLiveWorkoutBuilder,
                        didCollectDataOf collectedTypes: Set<HKSampleType>) {}
}
