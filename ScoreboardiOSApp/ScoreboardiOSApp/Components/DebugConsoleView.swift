import SwiftUI

/// Debug console view for displaying ESP32 thermal/memory/task data
struct DebugConsoleView: View {
    @Environment(ScoreboardViewModel.self) private var viewModel
    @State private var debugEnabled = false

    var body: some View {
        Section("Debug Console") {
            // Toggle to enable/disable debug logging
            Toggle("Enable Debug Logging", isOn: $debugEnabled)
                .onChange(of: debugEnabled) { _, newValue in
                    viewModel.bleManager.setDebugLogging(enabled: newValue)
                }

            if debugEnabled {
                if viewModel.bleManager.debugLogs.isEmpty {
                    HStack {
                        ProgressView()
                            .scaleEffect(0.8)
                        Text("Waiting for debug data...")
                            .foregroundColor(.secondary)
                            .font(.caption)
                    }
                    .padding(.vertical, 4)
                } else {
                    // Latest status summary
                    if let latest = viewModel.bleManager.debugLogs.first {
                        latestStatusView(latest)
                    }

                    // Log entries
                    ForEach(viewModel.bleManager.debugLogs.prefix(50)) { entry in
                        DebugLogRow(entry: entry)
                    }

                    // Action buttons
                    HStack {
                        Button(role: .destructive) {
                            viewModel.bleManager.clearDebugLogs()
                        } label: {
                            Label("Clear", systemImage: "trash")
                        }
                        .buttonStyle(.bordered)

                        Spacer()

                        Button {
                            copyLogsToClipboard()
                        } label: {
                            Label("Copy", systemImage: "doc.on.doc")
                        }
                        .buttonStyle(.bordered)
                    }
                    .padding(.vertical, 4)
                }
            }
        }
    }

    // MARK: - Latest Status Summary

    private func latestStatusView(_ entry: DebugLogEntry) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Latest Status")
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundColor(.secondary)
                Spacer()
                Text("Uptime: \(entry.uptimeString)")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }

            HStack(spacing: 16) {
                // Temperature
                VStack(alignment: .leading, spacing: 2) {
                    Text("Temp")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(entry.temperatureString)
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(entry.isOverheating ? .red : .primary)
                        .fontWeight(entry.isOverheating ? .bold : .regular)
                }

                Divider().frame(height: 30)

                // Heap
                VStack(alignment: .leading, spacing: 2) {
                    Text("Free Heap")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(entry.freeHeapKB)
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(entry.isLowMemory ? .orange : .primary)
                }

                Divider().frame(height: 30)

                // Min Heap
                VStack(alignment: .leading, spacing: 2) {
                    Text("Min Heap")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(entry.minHeapKB)
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(entry.isCriticalMemory ? .red : .secondary)
                }

                Divider().frame(height: 30)

                // Tasks
                VStack(alignment: .leading, spacing: 2) {
                    Text("Tasks")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text("\(entry.taskCount)")
                        .font(.system(.body, design: .monospaced))
                }
            }

            // Warning indicators
            if entry.isOverheating {
                Label("Overheating detected (>65C)", systemImage: "exclamationmark.triangle.fill")
                    .font(.caption)
                    .foregroundColor(.red)
            }
            if entry.isCriticalMemory {
                Label("Critical memory (<10KB)", systemImage: "exclamationmark.triangle.fill")
                    .font(.caption)
                    .foregroundColor(.red)
            } else if entry.isLowMemory {
                Label("Low memory (<20KB)", systemImage: "exclamationmark.triangle")
                    .font(.caption)
                    .foregroundColor(.orange)
            }
        }
        .padding(.vertical, 4)
    }

    // MARK: - Actions

    private func copyLogsToClipboard() {
        let text = viewModel.bleManager.debugLogs.map { entry in
            "[\(entry.formattedTime)] T:\(entry.temperatureString) Heap:\(entry.freeHeapKB) (min:\(entry.minHeapKB)) Tasks:\(entry.taskCount) Up:\(entry.uptimeString)"
        }.joined(separator: "\n")
        UIPasteboard.general.string = text
    }
}

/// Individual debug log row
struct DebugLogRow: View {
    let entry: DebugLogEntry

    var body: some View {
        HStack(spacing: 6) {
            Text(entry.formattedTime)
                .font(.system(.caption2, design: .monospaced))
                .foregroundColor(.secondary)
                .frame(width: 55, alignment: .leading)

            Text(entry.temperatureString)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(entry.isOverheating ? .red : .primary)
                .frame(width: 40, alignment: .trailing)

            Divider().frame(height: 12)

            Text(entry.freeHeapKB)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(entry.isLowMemory ? .orange : .primary)
                .frame(width: 45, alignment: .trailing)

            Text("(\(entry.minHeapKB))")
                .font(.system(.caption2, design: .monospaced))
                .foregroundColor(.secondary)
                .frame(width: 45, alignment: .trailing)

            Spacer()

            Text("\(entry.taskCount)")
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(.secondary)
        }
    }
}

#Preview {
    Form {
        DebugConsoleView()
    }
    .environment(ScoreboardViewModel())
}
