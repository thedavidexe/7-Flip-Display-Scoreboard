//
//  ScoreboardiOSAppApp.swift
//  ScoreboardiOSApp
//
//  Scoreboard Controller - iOS app for controlling ESP32-based
//  electromechanical 7-segment scoreboards via Bluetooth Low Energy.
//

import SwiftUI

@main
struct ScoreboardiOSAppApp: App {
    /// Central view model instance
    @State private var viewModel = ScoreboardViewModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(viewModel)
        }
    }
}
