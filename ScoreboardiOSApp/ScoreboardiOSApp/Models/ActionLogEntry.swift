import Foundation

/// Types of actions that can be logged
enum ActionType: String {
    case connected = "Connected"
    case disconnected = "Disconnected"
    case blueScoreChange = "Blue Score"
    case redScoreChange = "Red Score"
    case timerStarted = "Timer"
    case scoresReset = "Reset"
    case settingsChanged = "Settings"
}

/// A single entry in the action log
struct ActionLogEntry: Identifiable {
    /// Unique identifier
    let id = UUID()

    /// When the action occurred
    let timestamp: Date

    /// Hardware ID of the device
    let deviceId: String

    /// Type of action
    let type: ActionType

    /// Human-readable description
    let description: String

    /// Formatted time string (HH:mm:ss)
    var formattedTime: String {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss"
        return formatter.string(from: timestamp)
    }

    /// SF Symbol name for the action type
    var symbolName: String {
        switch type {
        case .connected:
            return "link"
        case .disconnected:
            return "link.badge.plus"
        case .blueScoreChange:
            return "plus.circle.fill"
        case .redScoreChange:
            return "plus.circle.fill"
        case .timerStarted:
            return "timer"
        case .scoresReset:
            return "arrow.counterclockwise"
        case .settingsChanged:
            return "gearshape"
        }
    }
}
