//
//  ContentView.swift
//  ScoreboardWatchApp Watch App
//
//  Root view that switches between selection and control screens.
//

import SwiftUI

struct ContentView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel

    var body: some View {
        Group {
            if viewModel.shouldShowControl {
                ScoreControlView()
            } else {
                ScoreboardSelectionView()
            }
        }
        .animation(.easeInOut, value: viewModel.shouldShowControl)
        .onChange(of: viewModel.bleManager.connectionStatus) { oldValue, newValue in
            if newValue == .connected && oldValue != .connected {
                viewModel.onConnected()
            } else if newValue == .disconnected && oldValue == .connected {
                viewModel.onDisconnected()
            }
        }
    }
}

#Preview {
    ContentView()
        .environment(ScoreboardViewModel())
}
