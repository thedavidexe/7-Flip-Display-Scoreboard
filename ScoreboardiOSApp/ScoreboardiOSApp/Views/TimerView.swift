import SwiftUI

/// Timer configuration view
struct TimerView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel
    @Environment(\.dismiss) private var dismiss

    @State private var minutes: Int = 5
    @State private var seconds: Int = 0

    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                // Timer display with pickers
                timerPicker

                // Preset buttons
                presetButtons

                Spacer()

                // Action buttons
                actionButtons
            }
            .padding()
            .navigationTitle("Timer")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel") {
                        dismiss()
                    }
                }
            }
        }
        .presentationDetents([.medium])
    }

    // MARK: - Subviews

    private var timerPicker: some View {
        HStack(spacing: 0) {
            // Minutes picker
            Picker("Minutes", selection: $minutes) {
                ForEach(0..<100) { value in
                    Text(String(format: "%02d", value))
                        .tag(value)
                }
            }
            .pickerStyle(.wheel)
            .frame(width: 80)

            Text(":")
                .font(.largeTitle.bold())
                .foregroundColor(.primary)

            // Seconds picker
            Picker("Seconds", selection: $seconds) {
                ForEach(0..<60) { value in
                    Text(String(format: "%02d", value))
                        .tag(value)
                }
            }
            .pickerStyle(.wheel)
            .frame(width: 80)
        }
        .frame(height: 150)
    }

    private var presetButtons: some View {
        HStack(spacing: 12) {
            TimerPresetButton(title: "10 min") {
                minutes = 10
                seconds = 0
            }

            TimerPresetButton(title: "5 min") {
                minutes = 5
                seconds = 0
            }

            TimerPresetButton(title: "1 min") {
                minutes = 1
                seconds = 0
            }

            TimerPresetButton(title: "30 sec") {
                minutes = 0
                seconds = 30
            }
        }
    }

    private var actionButtons: some View {
        VStack(spacing: 12) {
            Button(action: {
                viewModel.startTimer(minutes: UInt8(minutes), seconds: UInt8(seconds))
                dismiss()
            }) {
                Label("Start Timer", systemImage: "play.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(minutes == 0 && seconds == 0)

            Button(action: {
                viewModel.stopTimer()
                dismiss()
            }) {
                Text("Back to Score Mode")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
        }
    }
}
