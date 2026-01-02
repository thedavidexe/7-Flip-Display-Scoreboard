//
//  ContentView.swift
//  ScoreboardiOSApp
//
//  Root view that switches between selection and control screens.
//

import SwiftUI

struct ContentView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel

    var body: some View {
        Group {
            if viewModel.isConnected {
                ScoreControlView()
            } else {
                ScoreboardSelectionView()
            }
        }
        .animation(.easeInOut, value: viewModel.isConnected)
    }
}

#Preview {
    ContentView()
        .environment(ScoreboardViewModel())
}
