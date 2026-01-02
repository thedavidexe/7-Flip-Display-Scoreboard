import SwiftUI

/// Main score control view
struct ScoreControlView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel
    @State private var showSettings = false
    @State private var showTimer = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Connection status bar
                ConnectionStatusBar()

                // Score displays
                HStack(spacing: 0) {
                    // Blue team
                    TeamScoreView(
                        teamName: "BLUE",
                        teamColor: .blue,
                        score: viewModel.state.blueScore,
                        increment: viewModel.settings.scoreIncrement,
                        onIncrement: viewModel.incrementBlueScore,
                        onDecrement: viewModel.decrementBlueScore,
                        onScoreSet: viewModel.setBlueScore
                    )

                    Divider()

                    // Red team
                    TeamScoreView(
                        teamName: "RED",
                        teamColor: .red,
                        score: viewModel.state.redScore,
                        increment: viewModel.settings.scoreIncrement,
                        onIncrement: viewModel.incrementRedScore,
                        onDecrement: viewModel.decrementRedScore,
                        onScoreSet: viewModel.setRedScore
                    )
                }
                .frame(maxHeight: .infinity)

                // Bottom buttons
                VStack(spacing: 12) {
                    Button(action: viewModel.resetScores) {
                        Label("Reset Scores", systemImage: "arrow.counterclockwise")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)

                    Button(action: { showTimer = true }) {
                        Label("Set Timer", systemImage: "timer")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                }
                .padding()
            }
            .navigationTitle("Scoreboard")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: { showSettings = true }) {
                        Image(systemName: "gearshape")
                    }
                }
            }
            .sheet(isPresented: $showSettings) {
                SettingsView()
            }
            .sheet(isPresented: $showTimer) {
                TimerView()
            }
            .onChange(of: viewModel.bleManager.connectionStatus) { oldValue, newValue in
                if newValue == .disconnected && oldValue == .connected {
                    viewModel.onDisconnected()
                }
            }
        }
    }
}
