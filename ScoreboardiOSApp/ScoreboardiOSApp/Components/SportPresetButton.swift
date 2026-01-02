import SwiftUI

/// Preset button for sport score increment
struct SportPresetButton: View {
    @Environment(ScoreboardViewModel.self) private var viewModel

    let title: String
    let increment: Int

    var isSelected: Bool {
        viewModel.settings.scoreIncrement == increment
    }

    var body: some View {
        Button(action: {
            viewModel.settings.scoreIncrement = increment
        }) {
            VStack(spacing: 2) {
                Text(title)
                    .font(.caption)
                Text("+\(increment)")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
        }
        .buttonStyle(.bordered)
        .tint(isSelected ? .blue : .gray)
    }
}
