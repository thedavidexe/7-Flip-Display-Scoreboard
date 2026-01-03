import Foundation

/// Utility for building BLE packets
struct PacketBuilder {
    /// Build a 5-byte packet for scoreboard communication (score mode only for Watch)
    /// - Parameters:
    ///   - blueScore: Blue team score (0-99)
    ///   - redScore: Red team score (0-99)
    ///   - forceSegmentUpdate: Whether to force all segments to update (for reset/decrement)
    /// - Returns: 5-byte Data packet
    static func buildPacket(
        blueScore: UInt8,
        redScore: UInt8,
        forceSegmentUpdate: Bool = false
    ) -> Data {
        var packet = Data(count: Constants.packetSize)
        packet[Constants.PacketIndex.blueScore] = blueScore % 100
        packet[Constants.PacketIndex.redScore] = redScore % 100
        packet[Constants.PacketIndex.timerMinutes] = 0  // Watch doesn't support timer
        packet[Constants.PacketIndex.timerSeconds] = 0

        var flags: UInt8 = 0x00
        if forceSegmentUpdate {
            flags |= Constants.PacketFlags.forceSegmentUpdate
        }
        packet[Constants.PacketIndex.flags] = flags
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
}
