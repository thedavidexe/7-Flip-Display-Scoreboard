import Foundation
import CoreBluetooth

/// Represents a discovered BLE scoreboard device
struct ScoreboardDevice: Identifiable, Equatable {
    /// Unique identifier (peripheral UUID)
    let id: UUID

    /// CoreBluetooth peripheral reference
    let peripheral: CBPeripheral

    /// 4-character hardware ID displayed on the scoreboard
    let hardwareId: String

    /// Signal strength in dBm
    let rssi: Int

    /// Categorized signal strength
    var signalStrength: SignalStrength {
        switch rssi {
        case -50...0:
            return .strong
        case -70 ..< -50:
            return .good
        case -85 ..< -70:
            return .weak
        default:
            return .veryWeak
        }
    }

    /// Signal strength categories
    enum SignalStrength: String {
        case strong = "Strong"
        case good = "Good"
        case weak = "Weak"
        case veryWeak = "Very Weak"

        /// SF Symbol name for signal indicator
        var symbolName: String {
            switch self {
            case .strong:
                return "wifi.exclamationmark"
            case .good:
                return "wifi"
            case .weak:
                return "wifi"
            case .veryWeak:
                return "wifi.slash"
            }
        }

        /// Color for signal indicator
        var color: String {
            switch self {
            case .strong:
                return "green"
            case .good:
                return "blue"
            case .weak:
                return "orange"
            case .veryWeak:
                return "red"
            }
        }
    }

    static func == (lhs: ScoreboardDevice, rhs: ScoreboardDevice) -> Bool {
        lhs.id == rhs.id
    }
}
