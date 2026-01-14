import Foundation
import CoreBluetooth

/// Application-wide constants for the Scoreboard Controller
enum Constants {
    // MARK: - BLE UUIDs
    // Must match the ESP32 firmware UUIDs
    static let serviceUUID = CBUUID(string: "7B5E4A8C-2D1F-4E3B-9A6C-8F0D1E2C3B4A")
    static let characteristicUUID = CBUUID(string: "7B5E4A8C-2D1F-4E3B-9A6C-8F0D1E2C3B4B")

    // MARK: - Debug Logging
    // Debug characteristic for streaming thermal/memory/task data from ESP32
    static let debugCharacteristicUUID = CBUUID(string: "7B5E4A8C-2D1F-4E3B-9A6C-8F0D1E2C3B4C")
    static let debugPacketSize = 20
    static let maxDebugLogEntries = 200

    // MARK: - BLE Timeouts
    static let scanTimeout: TimeInterval = 10.0
    static let connectionTimeout: TimeInterval = 10.0
    static let writeRetryCount = 3
    static let writeRetryDelayBase: TimeInterval = 0.1

    // MARK: - Packet Protocol
    static let packetSize = 5

    // Packet byte indices
    enum PacketIndex {
        static let blueScore = 0
        static let redScore = 1
        static let timerMinutes = 2
        static let timerSeconds = 3
        static let flags = 4
    }

    // Flags byte bit definitions
    enum PacketFlags {
        static let timerUpdateSlow: UInt8 = 0x01      // bit 0: 1=10s updates, 0=1s updates
        static let forceSegmentUpdate: UInt8 = 0x02  // bit 1: 1=force all segments to update
    }

    // MARK: - Score Limits
    static let maxScore: UInt8 = 99
    static let minScore: UInt8 = 0

    // MARK: - Timer Limits
    static let maxTimerMinutes: UInt8 = 99
    static let maxTimerSeconds: UInt8 = 59

    // MARK: - Default Values
    static let defaultScoreIncrement = 1

    // MARK: - Action Log
    static let maxLogEntries = 100
}
