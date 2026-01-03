import Foundation

/// Persistent app settings stored in UserDefaults
@Observable
class AppSettings {
    /// Score increment value for the + button
    var scoreIncrement: Int {
        didSet { UserDefaults.standard.set(scoreIncrement, forKey: "scoreIncrement") }
    }

    /// Hardware ID of the last connected scoreboard
    var lastConnectedId: String? {
        didSet { UserDefaults.standard.set(lastConnectedId, forKey: "lastConnectedId") }
    }

    init() {
        self.scoreIncrement = UserDefaults.standard.object(forKey: "scoreIncrement") as? Int ?? 1
        self.lastConnectedId = UserDefaults.standard.string(forKey: "lastConnectedId")
    }
}

/// Sport presets for quick score increment configuration
enum SportPreset: String, CaseIterable, Identifiable {
    case generic = "+1"
    case basketball = "+2"
    case basketball3pt = "+3"
    case footballTD = "+6"
    case footballTDXP = "+7"

    var id: String { rawValue }

    /// Score increment for this sport
    var increment: Int {
        switch self {
        case .generic:
            return 1
        case .basketball:
            return 2
        case .basketball3pt:
            return 3
        case .footballTD:
            return 6
        case .footballTDXP:
            return 7
        }
    }
}
