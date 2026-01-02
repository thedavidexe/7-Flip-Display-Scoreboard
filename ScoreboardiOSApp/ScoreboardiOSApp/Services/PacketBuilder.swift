import Foundation

/// Utility for building BLE packets
struct PacketBuilder {
    /// Build a 5-byte packet for scoreboard communication
    /// - Parameters:
    ///   - blueScore: Blue team score (0-99)
    ///   - redScore: Red team score (0-99)
    ///   - timerMinutes: Timer minutes (0-99)
    ///   - timerSeconds: Timer seconds (0-59)
    ///   - slowTimerUpdates: Whether to use slow (10s) timer display updates
    /// - Returns: 5-byte Data packet
    static func buildPacket(
        blueScore: UInt8,
        redScore: UInt8,
        timerMinutes: UInt8,
        timerSeconds: UInt8,
        slowTimerUpdates: Bool
    ) -> Data {
        var packet = Data(count: Constants.packetSize)
        packet[Constants.PacketIndex.blueScore] = blueScore % 100
        packet[Constants.PacketIndex.redScore] = redScore % 100
        packet[Constants.PacketIndex.timerMinutes] = min(timerMinutes, Constants.maxTimerMinutes)
        packet[Constants.PacketIndex.timerSeconds] = min(timerSeconds, Constants.maxTimerSeconds)
        packet[Constants.PacketIndex.flags] = slowTimerUpdates ? Constants.PacketFlags.timerUpdateSlow : 0x00
        return packet
    }

    /// Wrap a score value to stay within 0-99 range
    /// - Parameter value: Input value (can be negative or > 99)
    /// - Returns: Wrapped value (0-99)
    static func wrapScore(_ value: Int) -> UInt8 {
        if value < 0 {
            return 0
        }
        return UInt8(value % 100)
    }

    /// Validate and clamp timer values
    /// - Parameters:
    ///   - minutes: Input minutes
    ///   - seconds: Input seconds
    /// - Returns: Tuple of validated (minutes, seconds)
    static func validateTimer(minutes: Int, seconds: Int) -> (minutes: UInt8, seconds: UInt8) {
        let validMinutes = UInt8(min(max(minutes, 0), Int(Constants.maxTimerMinutes)))
        let validSeconds = UInt8(min(max(seconds, 0), Int(Constants.maxTimerSeconds)))
        return (validMinutes, validSeconds)
    }
}
