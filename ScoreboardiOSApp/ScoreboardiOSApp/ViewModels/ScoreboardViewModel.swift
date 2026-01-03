import Foundation
import Combine

/// Central view model for scoreboard state management
@Observable
class ScoreboardViewModel {
    // MARK: - State

    /// Core scoreboard state (scores, timer, connection)
    var state = ScoreboardState()

    /// App settings (persisted)
    var settings = AppSettings()

    /// BLE communication manager
    let bleManager = BLEManager()

    // MARK: - Computed Properties

    /// Whether currently connected to a scoreboard
    var isConnected: Bool {
        bleManager.connectionStatus == .connected
    }

    /// Current connection status
    var connectionStatus: ConnectionStatus {
        bleManager.connectionStatus
    }

    // MARK: - Score Actions

    /// Increment blue team score by the configured amount
    func incrementBlueScore() {
        let newScore = Int(state.blueScore) + settings.scoreIncrement
        state.blueScore = PacketBuilder.wrapScore(newScore)
        logAction(.blueScoreChange, "Blue +\(settings.scoreIncrement) (now \(state.blueScore))")
        sendCurrentState()
    }

    /// Decrement blue team score by 1
    func decrementBlueScore() {
        if state.blueScore > 0 {
            state.blueScore -= 1
            logAction(.blueScoreChange, "Blue -1 (now \(state.blueScore))")
            sendCurrentState(forceUpdate: true)
        }
    }

    /// Increment red team score by the configured amount
    func incrementRedScore() {
        let newScore = Int(state.redScore) + settings.scoreIncrement
        state.redScore = PacketBuilder.wrapScore(newScore)
        logAction(.redScoreChange, "Red +\(settings.scoreIncrement) (now \(state.redScore))")
        sendCurrentState()
    }

    /// Decrement red team score by 1
    func decrementRedScore() {
        if state.redScore > 0 {
            state.redScore -= 1
            logAction(.redScoreChange, "Red -1 (now \(state.redScore))")
            sendCurrentState(forceUpdate: true)
        }
    }

    /// Set blue team score directly
    /// - Parameter score: New score value (0-99)
    func setBlueScore(_ score: UInt8) {
        state.blueScore = min(score, Constants.maxScore)
        logAction(.blueScoreChange, "Blue set to \(state.blueScore)")
        sendCurrentState()
    }

    /// Set red team score directly
    /// - Parameter score: New score value (0-99)
    func setRedScore(_ score: UInt8) {
        state.redScore = min(score, Constants.maxScore)
        logAction(.redScoreChange, "Red set to \(state.redScore)")
        sendCurrentState()
    }

    /// Reset both scores to 0 and stop any timer
    func resetScores() {
        state.reset()
        logAction(.scoresReset, "Scores reset to 00-00")
        sendCurrentState(forceUpdate: true)
    }

    // MARK: - Timer Actions

    /// Start a countdown timer
    /// - Parameters:
    ///   - minutes: Timer minutes (0-99)
    ///   - seconds: Timer seconds (0-59)
    func startTimer(minutes: UInt8, seconds: UInt8) {
        let validated = PacketBuilder.validateTimer(minutes: Int(minutes), seconds: Int(seconds))
        state.timerMinutes = validated.minutes
        state.timerSeconds = validated.seconds
        logAction(.timerStarted, "Timer \(String(format: "%02d:%02d", validated.minutes, validated.seconds))")
        sendCurrentState()
    }

    /// Stop the timer and return to score mode
    func stopTimer() {
        state.timerMinutes = 0
        state.timerSeconds = 0
        logAction(.timerStarted, "Timer stopped, back to score mode")
        sendCurrentState()
    }

    // MARK: - BLE Communication

    /// Send the current state to the scoreboard
    /// - Parameter forceUpdate: If true, sets the force segment update flag to refresh all display segments
    func sendCurrentState(forceUpdate: Bool = false) {
        guard isConnected else { return }

        let packet = state.toPacket(slowTimerUpdates: settings.slowTimerUpdates, forceSegmentUpdate: forceUpdate)
        Task {
            let success = await bleManager.sendPacket(packet)
            if !success {
                // Could show error to user
                print("Failed to send packet after retries")
            }
        }
    }

    /// Send initial zero state packet after connection
    func sendInitialPacket() {
        let packet = PacketBuilder.buildPacket(
            blueScore: 0,
            redScore: 0,
            timerMinutes: 0,
            timerSeconds: 0,
            slowTimerUpdates: false
        )
        Task {
            _ = await bleManager.sendPacket(packet)
        }
    }

    /// Called when connection is established
    func onConnected() {
        if let device = bleManager.connectedDevice {
            state.connectedDeviceId = device.hardwareId
            settings.lastConnectedId = device.hardwareId
            logAction(.connected, "Connected to \(device.hardwareId)")
        }
        state.connectionStatus = .connected
        sendInitialPacket()
    }

    /// Called when disconnected
    func onDisconnected() {
        if let deviceId = state.connectedDeviceId {
            logAction(.disconnected, "Disconnected from \(deviceId)")
        }
        state.connectionStatus = .disconnected
        state.connectedDeviceId = nil
    }

    // MARK: - Logging

    /// Add an entry to the action log
    /// - Parameters:
    ///   - type: Type of action
    ///   - description: Human-readable description
    private func logAction(_ type: ActionType, _ description: String) {
        let entry = ActionLogEntry(
            timestamp: Date(),
            deviceId: bleManager.connectedDevice?.hardwareId ?? state.connectedDeviceId ?? "Unknown",
            type: type,
            description: description
        )
        state.actionLog.insert(entry, at: 0)

        // Keep only recent entries
        if state.actionLog.count > Constants.maxLogEntries {
            state.actionLog.removeLast()
        }
    }

    /// Clear all log entries
    func clearLog() {
        state.actionLog.removeAll()
    }
}
