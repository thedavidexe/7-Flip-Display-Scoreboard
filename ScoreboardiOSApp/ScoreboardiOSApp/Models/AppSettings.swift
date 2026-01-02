import Foundation
import SwiftUI

/// Persistent app settings stored in UserDefaults
@Observable
class AppSettings {
    /// Score increment value for the large + button
    var scoreIncrement: Int {
        didSet { UserDefaults.standard.set(scoreIncrement, forKey: "scoreIncrement") }
    }

    /// Whether to use slow (10s) timer display updates
    var slowTimerUpdates: Bool {
        didSet { UserDefaults.standard.set(slowTimerUpdates, forKey: "slowTimerUpdates") }
    }

    /// Hardware ID of the last connected scoreboard
    var lastConnectedId: String? {
        didSet { UserDefaults.standard.set(lastConnectedId, forKey: "lastConnectedId") }
    }

    init() {
        self.scoreIncrement = UserDefaults.standard.object(forKey: "scoreIncrement") as? Int ?? 1
        self.slowTimerUpdates = UserDefaults.standard.bool(forKey: "slowTimerUpdates")
        self.lastConnectedId = UserDefaults.standard.string(forKey: "lastConnectedId")
    }
}

/// Sport presets for quick score increment configuration
enum SportPreset: String, CaseIterable, Identifiable {
    case generic = "Generic"
    case basketball = "Basketball"
    case basketball3pt = "Basketball 3pt"
    case footballTD = "Football TD"
    case footballTDXP = "Football TD+XP"
    case rugby = "Rugby Try"
    case rugbyConverted = "Rugby Conv."
    case soccer = "Soccer/Hockey"

    var id: String { rawValue }

    /// Score increment for this sport
    var increment: Int {
        switch self {
        case .generic, .soccer:
            return 1
        case .basketball:
            return 2
        case .basketball3pt:
            return 3
        case .rugby:
            return 5
        case .footballTD:
            return 6
        case .footballTDXP, .rugbyConverted:
            return 7
        }
    }

    /// Short display label
    var shortLabel: String {
        switch self {
        case .generic:
            return "+1"
        case .basketball:
            return "+2"
        case .basketball3pt:
            return "+3"
        case .rugby:
            return "+5"
        case .footballTD:
            return "+6"
        case .footballTDXP, .rugbyConverted:
            return "+7"
        case .soccer:
            return "+1"
        }
    }
}
