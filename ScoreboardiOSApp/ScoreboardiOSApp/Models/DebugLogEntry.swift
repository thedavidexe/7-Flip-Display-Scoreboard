import Foundation

/// Represents a debug log entry received from the ESP32
struct DebugLogEntry: Identifiable {
    let id = UUID()
    let timestamp: Date
    let sequence: UInt8
    let uptimeSeconds: UInt16
    let temperature: Float       // Celsius
    let freeHeap: UInt32         // Bytes
    let minHeap: UInt32          // Bytes
    let rssi: Int8               // dBm
    let taskCount: UInt8

    // MARK: - Formatted Display Properties

    var formattedTime: String {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss"
        return formatter.string(from: timestamp)
    }

    var temperatureString: String {
        if temperature < -900 {
            return "N/A"  // Sensor error indicator
        }
        return String(format: "%.1fC", temperature)
    }

    var freeHeapKB: String {
        String(format: "%.0fKB", Float(freeHeap) / 1024.0)
    }

    var minHeapKB: String {
        String(format: "%.0fKB", Float(minHeap) / 1024.0)
    }

    var uptimeString: String {
        let hours = uptimeSeconds / 3600
        let minutes = (uptimeSeconds % 3600) / 60
        let seconds = uptimeSeconds % 60
        if hours > 0 {
            return String(format: "%dh%02dm", hours, minutes)
        } else if minutes > 0 {
            return String(format: "%dm%02ds", minutes, seconds)
        } else {
            return String(format: "%ds", seconds)
        }
    }

    // MARK: - Threshold Warnings

    /// ESP32 thermal throttle threshold is around 65C
    var isOverheating: Bool {
        temperature > 65.0 && temperature < 900  // Exclude error values
    }

    /// Low memory warning threshold (20KB)
    var isLowMemory: Bool {
        freeHeap < 20_000
    }

    /// Critical memory warning threshold (10KB)
    var isCriticalMemory: Bool {
        freeHeap < 10_000
    }

    // MARK: - Parsing

    /// Parse a 20-byte debug packet from ESP32
    static func fromPacket(_ data: Data) -> DebugLogEntry? {
        guard data.count >= Constants.debugPacketSize else { return nil }

        // Byte 0: Packet type (should be 0x04 for full status)
        let packetType = data[0]
        guard packetType == 0x04 else { return nil }

        // Byte 1: Sequence number
        let sequence = data[1]

        // Bytes 2-3: Uptime (uint16_t, little-endian)
        let uptime = UInt16(data[2]) | (UInt16(data[3]) << 8)

        // Bytes 4-7: Temperature (float, little-endian)
        let tempBytes = Data(data[4..<8])
        let temperature = tempBytes.withUnsafeBytes { $0.load(as: Float.self) }

        // Bytes 8-11: Free heap (uint32_t, little-endian)
        let freeHeap = UInt32(data[8]) |
                       (UInt32(data[9]) << 8) |
                       (UInt32(data[10]) << 16) |
                       (UInt32(data[11]) << 24)

        // Bytes 12-15: Min heap (uint32_t, little-endian)
        let minHeap = UInt32(data[12]) |
                      (UInt32(data[13]) << 8) |
                      (UInt32(data[14]) << 16) |
                      (UInt32(data[15]) << 24)

        // Byte 16: RSSI (int8_t)
        let rssi = Int8(bitPattern: data[16])

        // Byte 17: Task count
        let taskCount = data[17]

        return DebugLogEntry(
            timestamp: Date(),
            sequence: sequence,
            uptimeSeconds: uptime,
            temperature: temperature,
            freeHeap: freeHeap,
            minHeap: minHeap,
            rssi: rssi,
            taskCount: taskCount
        )
    }
}
