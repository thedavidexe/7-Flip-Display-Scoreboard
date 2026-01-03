import Foundation

/// Connection status for BLE communication
enum ConnectionStatus: String {
    case disconnected = "Disconnected"
    case scanning = "Scanning..."
    case connecting = "Connecting..."
    case connected = "Connected"
    case reconnecting = "Reconnecting..."
}

/// Core state model for the scoreboard (watch version - no timer)
@Observable
class ScoreboardState {
    /// Blue team score (0-99)
    var blueScore: UInt8 = 0

    /// Red team score (0-99)
    var redScore: UInt8 = 0

    /// BLE connection status
    var connectionStatus: ConnectionStatus = .disconnected

    /// Hardware ID of connected scoreboard
    var connectedDeviceId: String?

    /// Build a 5-byte packet for BLE transmission (score mode only)
    /// - Parameter forceSegmentUpdate: Whether to force all segments to update
    /// - Returns: 5-byte Data packet
    func toPacket(forceSegmentUpdate: Bool = false) -> Data {
        return PacketBuilder.buildPacket(
            blueScore: blueScore,
            redScore: redScore,
            forceSegmentUpdate: forceSegmentUpdate
        )
    }

    /// Reset both scores to zero
    func reset() {
        blueScore = 0
        redScore = 0
    }
}
