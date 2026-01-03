import SwiftUI

/// Main score control view for Apple Watch
struct ScoreControlView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel
    @State private var showingSettings = false
    @State private var showingResetConfirm = false

    var body: some View {
        GeometryReader { geometry in
            HStack(spacing: 4) {
                // Blue team
                teamColumn(
                    score: viewModel.state.blueScore,
                    color: .blue,
                    onIncrement: viewModel.incrementBlueScore,
                    onDecrement: viewModel.decrementBlueScore,
                    height: geometry.size.height,
                    showReset: $showingResetConfirm
                )

                // Red team
                teamColumn(
                    score: viewModel.state.redScore,
                    color: .red,
                    onIncrement: viewModel.incrementRedScore,
                    onDecrement: viewModel.decrementRedScore,
                    height: geometry.size.height,
                    showReset: $showingResetConfirm
                )
            }
            .padding(.horizontal, 2)
        }
        .confirmationDialog("Options", isPresented: $showingResetConfirm) {
            Button("Reset to 00-00", role: .destructive) {
                viewModel.resetScores()
            }
            Button("Disconnect", role: .destructive) {
                viewModel.bleManager.disconnect()
            }
            Button("Cancel", role: .cancel) {}
        }
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    showingSettings = true
                } label: {
                    Image(systemName: "gearshape")
                }
            }
        }
        .sheet(isPresented: $showingSettings) {
            SettingsView()
        }
    }

    private func teamColumn(
        score: UInt8,
        color: Color,
        onIncrement: @escaping () -> Void,
        onDecrement: @escaping () -> Void,
        height: CGFloat,
        showReset: Binding<Bool>
    ) -> some View {
        VStack(spacing: 4) {
            // Plus button with score inside (70% height)
            Button {
                onIncrement()
            } label: {
                VStack(spacing: 2) {
                    Text(String(format: "%02d", score))
                        .font(.system(size: 36, weight: .bold, design: .monospaced))
                    Text("+\(viewModel.settings.scoreIncrement)")
                        .font(.caption)
                        .fontWeight(.semibold)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(color)
            .frame(height: height * 0.70)
            .simultaneousGesture(
                LongPressGesture(minimumDuration: 0.5)
                    .onEnded { _ in showReset.wrappedValue = true }
            )

            // Minus button below
            Button {
                onDecrement()
            } label: {
                Image(systemName: "minus")
                    .font(.body)
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .tint(.gray)
            .simultaneousGesture(
                LongPressGesture(minimumDuration: 0.5)
                    .onEnded { _ in showReset.wrappedValue = true }
            )
        }
    }
}
