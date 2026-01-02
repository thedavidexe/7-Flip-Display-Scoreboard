import SwiftUI

/// View for scanning and selecting a scoreboard device
struct ScoreboardSelectionView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel

    var body: some View {
        NavigationStack {
            VStack(spacing: 20) {
                // Status indicator
                statusSection

                // Device list
                if viewModel.bleManager.discoveredDevices.isEmpty {
                    emptyState
                } else {
                    deviceList
                }

                // Scan button
                scanButton
            }
            .navigationTitle("Select Scoreboard")
            .onAppear {
                viewModel.bleManager.startScanning()
            }
            .onChange(of: viewModel.bleManager.connectionStatus) { oldValue, newValue in
                if newValue == .connected {
                    viewModel.onConnected()
                }
            }
        }
    }

    // MARK: - Subviews

    private var statusSection: some View {
        VStack(spacing: 8) {
            if viewModel.connectionStatus == .scanning {
                ProgressView()
                    .scaleEffect(1.2)
                Text("Scanning for scoreboards...")
                    .foregroundColor(.secondary)
            } else if viewModel.connectionStatus == .connecting {
                ProgressView()
                    .scaleEffect(1.2)
                Text("Connecting...")
                    .foregroundColor(.secondary)
            } else if let error = viewModel.bleManager.lastError {
                Image(systemName: "exclamationmark.triangle")
                    .foregroundColor(.orange)
                    .font(.title2)
                Text(error)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
            }
        }
        .padding()
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(systemName: "antenna.radiowaves.left.and.right.slash")
                .font(.system(size: 50))
                .foregroundColor(.secondary)
            Text("No scoreboards found")
                .font(.headline)
                .foregroundColor(.secondary)
            Text("Make sure your scoreboard is powered on")
                .font(.subheadline)
                .foregroundColor(.secondary)
        }
        .frame(maxHeight: .infinity)
    }

    private var deviceList: some View {
        List(viewModel.bleManager.discoveredDevices) { device in
            DeviceListRow(device: device) {
                viewModel.bleManager.connect(to: device)
            }
        }
        .listStyle(.insetGrouped)
    }

    private var scanButton: some View {
        Button(action: {
            viewModel.bleManager.startScanning()
        }) {
            Label("Scan for Scoreboards", systemImage: "antenna.radiowaves.left.and.right")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(.borderedProminent)
        .disabled(viewModel.connectionStatus == .scanning)
        .padding(.horizontal)
        .padding(.bottom)
    }
}
