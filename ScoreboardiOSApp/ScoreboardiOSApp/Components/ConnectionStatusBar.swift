import SwiftUI

/// Status bar showing connection state
struct ConnectionStatusBar: View {
    @Environment(ScoreboardViewModel.self) private var viewModel

    private var statusColor: Color {
        switch viewModel.bleManager.connectionStatus {
        case .connected:
            return .green
        case .reconnecting:
            return .yellow
        case .connecting:
            return .orange
        case .scanning:
            return .blue
        case .disconnected:
            return .red
        }
    }

    var body: some View {
        HStack(spacing: 8) {
            // Status indicator
            Circle()
                .fill(statusColor)
                .frame(width: 10, height: 10)

            // Device info or status
            if let device = viewModel.bleManager.connectedDevice {
                Text("Connected: \(device.hardwareId)")
                    .font(.subheadline)
            } else {
                Text(viewModel.bleManager.connectionStatus.rawValue)
                    .font(.subheadline)
            }

            Spacer()

            // Signal strength indicator
            if let device = viewModel.bleManager.connectedDevice {
                Image(systemName: "antenna.radiowaves.left.and.right")
                    .foregroundColor(signalColor(for: device.signalStrength))
                    .font(.caption)
            }
        }
        .padding(.horizontal)
        .padding(.vertical, 8)
        .background(Color(.systemGray6))
    }

    private func signalColor(for strength: ScoreboardDevice.SignalStrength) -> Color {
        switch strength {
        case .strong:
            return .green
        case .good:
            return .blue
        case .weak:
            return .orange
        case .veryWeak:
            return .red
        }
    }
}
