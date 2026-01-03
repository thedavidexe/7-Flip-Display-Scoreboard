import Foundation

/// Central view model for scoreboard state management (watch version)
@Observable
class ScoreboardViewModel {
    // MARK: - State

    /// Core scoreboard state (scores, connection)
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
        sendCurrentState()
    }

    /// Decrement blue team score by 1
    func decrementBlueScore() {
        if state.blueScore > 0 {
            state.blueScore -= 1
            sendCurrentState(forceUpdate: true)
        }
    }

    /// Increment red team score by the configured amount
    func incrementRedScore() {
        let newScore = Int(state.redScore) + settings.scoreIncrement
        state.redScore = PacketBuilder.wrapScore(newScore)
        sendCurrentState()
    }

    /// Decrement red team score by 1
    func decrementRedScore() {
        if state.redScore > 0 {
            state.redScore -= 1
            sendCurrentState(forceUpdate: true)
        }
    }

    /// Reset both scores to 0
    func resetScores() {
        state.reset()
        sendCurrentState(forceUpdate: true)
    }

    // MARK: - BLE Communication

    /// Send the current state to the scoreboard
    /// - Parameter forceUpdate: If true, sets the force segment update flag
    func sendCurrentState(forceUpdate: Bool = false) {
        guard isConnected else { return }

        let packet = state.toPacket(forceSegmentUpdate: forceUpdate)
        Task {
            _ = await bleManager.sendPacket(packet)
        }
    }

    /// Send initial zero state packet after connection
    func sendInitialPacket() {
        let packet = PacketBuilder.buildPacket(
            blueScore: 0,
            redScore: 0,
            forceSegmentUpdate: false
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
        }
        state.connectionStatus = .connected
        sendInitialPacket()
    }

    /// Called when disconnected
    func onDisconnected() {
        state.connectionStatus = .disconnected
        state.connectedDeviceId = nil
    }
}
