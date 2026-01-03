//
//  ScoreboardWatchAppApp.swift
//  ScoreboardWatchApp Watch App
//
//  Scoreboard Controller - watchOS app for controlling ESP32-based
//  electromechanical 7-segment scoreboards via Bluetooth Low Energy.
//

import SwiftUI

@main
struct ScoreboardWatchApp_Watch_AppApp: App {
    /// Central view model instance
    @State private var viewModel = ScoreboardViewModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(viewModel)
        }
    }
}
