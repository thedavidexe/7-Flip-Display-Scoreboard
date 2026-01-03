import SwiftUI

/// View for discovering and connecting to scoreboards
struct ScoreboardSelectionView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel

    var body: some View {
        NavigationStack {
            Group {
                if viewModel.bleManager.connectionStatus == .scanning {
                    scanningView
                } else if viewModel.bleManager.discoveredDevices.isEmpty {
                    emptyStateView
                } else {
                    deviceListView
                }
            }
            .navigationTitle("Scoreboards")
            .toolbar {
                ToolbarItem(placement: .bottomBar) {
                    scanButton
                }
            }
        }
    }

    private var scanningView: some View {
        VStack(spacing: 8) {
            ProgressView()
            Text("Scanning...")
                .font(.footnote)
                .foregroundColor(.secondary)
        }
    }

    private var emptyStateView: some View {
        VStack(spacing: 8) {
            Image(systemName: "antenna.radiowaves.left.and.right.slash")
                .font(.title2)
                .foregroundColor(.secondary)
            Text("No scoreboards found")
                .font(.footnote)
                .foregroundColor(.secondary)
        }
    }

    private var deviceListView: some View {
        List(viewModel.bleManager.discoveredDevices) { device in
            Button {
                viewModel.bleManager.connect(to: device)
            } label: {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(device.hardwareId)
                            .font(.headline)
                        Text(device.signalStrength.rawValue)
                            .font(.caption2)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    signalIndicator(for: device)
                }
            }
        }
    }

    private func signalIndicator(for device: ScoreboardDevice) -> some View {
        Image(systemName: device.signalStrength.symbolName)
            .foregroundColor(colorFor(device.signalStrength))
            .font(.footnote)
    }

    private func colorFor(_ strength: ScoreboardDevice.SignalStrength) -> Color {
        switch strength {
        case .strong: return .green
        case .good: return .blue
        case .weak: return .orange
        case .veryWeak: return .red
        }
    }

    private var scanButton: some View {
        Button {
            viewModel.bleManager.startScanning()
        } label: {
            Label("Scan", systemImage: "arrow.clockwise")
        }
        .disabled(viewModel.bleManager.connectionStatus == .scanning)
    }
}
