import SwiftUI

/// Preset button for timer quick selection
struct TimerPresetButton: View {
    let title: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.subheadline)
                .padding(.horizontal, 16)
                .padding(.vertical, 8)
        }
        .buttonStyle(.bordered)
    }
}
