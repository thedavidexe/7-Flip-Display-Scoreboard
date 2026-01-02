import SwiftUI

/// Settings and action log view
struct SettingsView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                // Score increment section
                scoreIncrementSection

                // Power saving section
                powerSavingSection

                // Connection section
                connectionSection

                // Action log section
                actionLogSection
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") {
                        dismiss()
                    }
                }
            }
        }
    }

    // MARK: - Sections

    private var scoreIncrementSection: some View {
        Section("Score Increment") {
            // Increment picker
            Picker("Increment Amount", selection: Bindable(viewModel).settings.scoreIncrement) {
                Text("+1").tag(1)
                Text("+2").tag(2)
                Text("+3").tag(3)
                Text("+5").tag(5)
                Text("+6").tag(6)
                Text("+7").tag(7)
            }
            .pickerStyle(.segmented)

            // Sport presets
            VStack(alignment: .leading, spacing: 8) {
                Text("Sport Presets")
                    .font(.subheadline)
                    .foregroundColor(.secondary)

                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        SportPresetButton(title: "Basketball", increment: 2)
                        SportPresetButton(title: "3-Point", increment: 3)
                        SportPresetButton(title: "Football TD", increment: 6)
                        SportPresetButton(title: "Soccer", increment: 1)
                    }
                }
            }
        }
    }

    private var powerSavingSection: some View {
        Section("Power Saving") {
            Toggle("Slow timer updates (10s)", isOn: Bindable(viewModel).settings.slowTimerUpdates)

            Text("When enabled, the display only updates every 10 seconds during timer mode to reduce power consumption.")
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    private var connectionSection: some View {
        Section("Connection") {
            if let device = viewModel.bleManager.connectedDevice {
                HStack {
                    Text("Connected to")
                    Spacer()
                    Text(device.hardwareId)
                        .foregroundColor(.secondary)
                        .fontWeight(.medium)
                }

                HStack {
                    Text("Signal")
                    Spacer()
                    Text(device.signalStrength.rawValue)
                        .foregroundColor(.secondary)
                }

                Button("Disconnect", role: .destructive) {
                    viewModel.bleManager.disconnect()
                    dismiss()
                }
            } else {
                Text("Not connected")
                    .foregroundColor(.secondary)
            }
        }
    }

    private var actionLogSection: some View {
        Section("Action Log") {
            if viewModel.state.actionLog.isEmpty {
                Text("No actions recorded")
                    .foregroundColor(.secondary)
            } else {
                ForEach(viewModel.state.actionLog.prefix(20)) { entry in
                    HStack {
                        Image(systemName: entry.symbolName)
                            .foregroundColor(.secondary)
                            .frame(width: 20)

                        Text(entry.formattedTime)
                            .font(.caption)
                            .foregroundColor(.secondary)
                            .frame(width: 60, alignment: .leading)

                        Text(entry.description)
                            .font(.subheadline)
                    }
                }

                if !viewModel.state.actionLog.isEmpty {
                    Button("Clear Log", role: .destructive) {
                        viewModel.clearLog()
                    }
                }
            }
        }
    }
}
