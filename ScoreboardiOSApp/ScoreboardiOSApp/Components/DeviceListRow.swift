import SwiftUI

/// Row displaying a discovered scoreboard device
struct DeviceListRow: View {
    let device: ScoreboardDevice
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 12) {
                // Signal indicator
                Image(systemName: "antenna.radiowaves.left.and.right")
                    .foregroundColor(signalColor)
                    .font(.title2)
                    .frame(width: 30)

                // Device info
                VStack(alignment: .leading, spacing: 4) {
                    Text("Scoreboard \(device.hardwareId)")
                        .font(.headline)
                        .foregroundColor(.primary)

                    HStack(spacing: 4) {
                        Text("Signal:")
                            .foregroundColor(.secondary)
                        Text(device.signalStrength.rawValue)
                            .foregroundColor(signalColor)
                    }
                    .font(.caption)
                }

                Spacer()

                // Chevron
                Image(systemName: "chevron.right")
                    .foregroundColor(.secondary)
                    .font(.caption)
            }
            .padding(.vertical, 4)
        }
        .buttonStyle(.plain)
    }

    private var signalColor: Color {
        switch device.signalStrength {
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
