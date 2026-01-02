import Foundation

/// Connection status for BLE communication
enum ConnectionStatus: String {
    case disconnected = "Disconnected"
    case scanning = "Scanning..."
    case connecting = "Connecting..."
    case connected = "Connected"
    case reconnecting = "Reconnecting..."
}

/// Core state model for the scoreboard
@Observable
class ScoreboardState {
    /// Blue team score (0-99)
    var blueScore: UInt8 = 0

    /// Red team score (0-99)
    var redScore: UInt8 = 0

    /// Timer minutes (0-99, 0 when in score mode)
    var timerMinutes: UInt8 = 0

    /// Timer seconds (0-59)
    var timerSeconds: UInt8 = 0

    /// BLE connection status
    var connectionStatus: ConnectionStatus = .disconnected

    /// Hardware ID of connected scoreboard
    var connectedDeviceId: String?

    /// Action history log
    var actionLog: [ActionLogEntry] = []

    /// Build a 5-byte packet for BLE transmission
    /// - Parameter slowTimerUpdates: Whether to use slow (10s) timer display updates
    /// - Returns: 5-byte Data packet
    func toPacket(slowTimerUpdates: Bool) -> Data {
        var packet = Data(count: Constants.packetSize)
        packet[Constants.PacketIndex.blueScore] = blueScore % 100
        packet[Constants.PacketIndex.redScore] = redScore % 100
        packet[Constants.PacketIndex.timerMinutes] = min(timerMinutes, Constants.maxTimerMinutes)
        packet[Constants.PacketIndex.timerSeconds] = min(timerSeconds, Constants.maxTimerSeconds)
        packet[Constants.PacketIndex.flags] = slowTimerUpdates ? Constants.PacketFlags.timerUpdateSlow : 0x00
        return packet
    }

    /// Reset all scores and timer to zero
    func reset() {
        blueScore = 0
        redScore = 0
        timerMinutes = 0
        timerSeconds = 0
    }

    /// Check if timer mode is active
    var isTimerMode: Bool {
        timerMinutes > 0 || timerSeconds > 0
    }

    /// Formatted timer string (MM:SS)
    var timerFormatted: String {
        String(format: "%02d:%02d", timerMinutes, timerSeconds)
    }
}
