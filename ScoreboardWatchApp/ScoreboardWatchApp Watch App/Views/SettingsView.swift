import SwiftUI

/// Settings view for the watch app
struct SettingsView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                // Score increment section
                Section("Score Increment") {
                    ForEach(SportPreset.allCases) { preset in
                        Button {
                            viewModel.settings.scoreIncrement = preset.increment
                        } label: {
                            HStack {
                                Text(preset.rawValue)
                                Spacer()
                                if viewModel.settings.scoreIncrement == preset.increment {
                                    Image(systemName: "checkmark")
                                        .foregroundColor(.blue)
                                }
                            }
                        }
                        .foregroundColor(.primary)
                    }
                }

                // Connection section
                Section("Connection") {
                    if let deviceId = viewModel.state.connectedDeviceId {
                        HStack {
                            Text("Device")
                            Spacer()
                            Text(deviceId)
                                .foregroundColor(.secondary)
                        }
                    }

                    Button(role: .destructive) {
                        viewModel.bleManager.disconnect()
                        dismiss()
                    } label: {
                        Text("Disconnect")
                    }
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        dismiss()
                    }
                }
            }
        }
    }
}
