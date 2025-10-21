//
// ChannelMappingView.swift
// AES67 Manager - Build #17
// Full 128-channel interactive mapping visualizer
//

import SwiftUI

struct ChannelMappingView: View {
    @ObservedObject var driverManager: DriverManager
    @State private var selectedChannel: Int? = nil
    @State private var selectedStream: StreamInfo? = nil
    @State private var hoveredChannel: Int? = nil

    private let totalChannels = 128
    private let channelsPerRow = 16
    private let rows = 8

    var body: some View {
        VStack(spacing: 0) {
            // Header
            headerView

            Divider()

            HStack(alignment: .top, spacing: 20) {
                // Channel Grid
                VStack(alignment: .leading, spacing: 12) {
                    Text("128-Channel Device Layout")
                        .font(.headline)
                        .padding(.horizontal)

                    channelGridView
                        .padding(.horizontal)

                    channelInfoBar
                        .padding(.horizontal)
                }
                .frame(maxWidth: .infinity)

                Divider()

                // Stream List & Controls
                VStack(alignment: .leading, spacing: 12) {
                    Text("Active Streams")
                        .font(.headline)

                    streamListView

                    Spacer()

                    if let stream = selectedStream {
                        mappingControlsView(for: stream)
                    }
                }
                .frame(width: 280)
                .padding()
            }
            .frame(maxHeight: .infinity)
        }
        .background(Color(nsColor: .controlBackgroundColor))
    }

    // MARK: - Header View

    private var headerView: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text("Channel Mapping")
                    .font(.title2)
                    .fontWeight(.bold)

                Text("\(usedChannelCount)/128 channels assigned")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            }

            Spacer()

            // Legend
            HStack(spacing: 16) {
                LegendItem(color: .gray.opacity(0.2), label: "Available")
                LegendItem(color: .blue, label: "Assigned")
                LegendItem(color: .green, label: "Selected")
            }
        }
        .padding()
    }

    // MARK: - Channel Grid View

    private var channelGridView: some View {
        VStack(spacing: 2) {
            ForEach(0..<rows, id: \.self) { row in
                HStack(spacing: 2) {
                    // Row label
                    Text("\(row * channelsPerRow)")
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundColor(.secondary)
                        .frame(width: 30, alignment: .trailing)

                    // Channel cells
                    ForEach(0..<channelsPerRow, id: \.self) { col in
                        let channelNum = row * channelsPerRow + col
                        ChannelCell(
                            channelNumber: channelNum,
                            mapping: mappingForChannel(channelNum),
                            isSelected: selectedChannel == channelNum,
                            isHovered: hoveredChannel == channelNum,
                            isInSelectedStream: isChannelInSelectedStream(channelNum)
                        )
                        .onTapGesture {
                            handleChannelTap(channelNum)
                        }
                        .onHover { hovering in
                            hoveredChannel = hovering ? channelNum : nil
                        }
                    }
                }
            }

            // Column labels
            HStack(spacing: 2) {
                Spacer()
                    .frame(width: 30)

                ForEach(0..<channelsPerRow, id: \.self) { col in
                    Text("\(col)")
                        .font(.system(size: 9, design: .monospaced))
                        .foregroundColor(.secondary)
                        .frame(maxWidth: .infinity)
                }
            }
        }
        .padding(8)
        .background(Color(nsColor: .textBackgroundColor))
        .cornerRadius(8)
    }

    // MARK: - Channel Info Bar

    private var channelInfoBar: some View {
        HStack {
            if let channel = hoveredChannel ?? selectedChannel {
                Text("Channel \(channel)")
                    .font(.system(.body, design: .monospaced))
                    .fontWeight(.semibold)

                if let mapping = mappingForChannel(channel) {
                    Text("•")
                        .foregroundColor(.secondary)
                    Text(mapping.streamName)
                        .foregroundColor(.blue)
                    Text("(Stream Ch \(channel - Int(mapping.deviceChannelStart)))")
                        .font(.caption)
                        .foregroundColor(.secondary)
                } else {
                    Text("•")
                        .foregroundColor(.secondary)
                    Text("Available")
                        .foregroundColor(.secondary)
                }
            } else {
                Text("Hover over a channel for details")
                    .foregroundColor(.secondary)
            }

            Spacer()
        }
        .padding(8)
        .background(Color(nsColor: .controlBackgroundColor))
        .cornerRadius(6)
    }

    // MARK: - Stream List View

    private var streamListView: some View {
        ScrollView {
            VStack(spacing: 8) {
                ForEach(driverManager.streams) { stream in
                    StreamMappingRow(
                        stream: stream,
                        isSelected: selectedStream?.id == stream.id,
                        color: colorForStream(stream)
                    )
                    .onTapGesture {
                        selectedStream = stream
                    }
                }
            }
        }
    }

    // MARK: - Mapping Controls

    private func mappingControlsView(for stream: StreamInfo) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Divider()

            Text("Map Stream")
                .font(.headline)

            VStack(alignment: .leading, spacing: 8) {
                Text(stream.name)
                    .font(.subheadline)
                    .fontWeight(.medium)

                Text("\(stream.numChannels) channels")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            if let mapping = stream.mapping {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Current Mapping:")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Text("Device Ch \(mapping.deviceChannelStart)-\(mapping.deviceChannelStart + mapping.deviceChannelCount - 1)")
                        .font(.system(.body, design: .monospaced))
                }
                .padding(8)
                .background(Color.blue.opacity(0.1))
                .cornerRadius(6)

                Button("Clear Mapping") {
                    clearMapping(for: stream)
                }
                .buttonStyle(.bordered)
            } else {
                Button("Auto-Assign Channels") {
                    autoAssignChannels(for: stream)
                }
                .buttonStyle(.borderedProminent)
            }
        }
        .padding(12)
        .background(Color(nsColor: .controlBackgroundColor))
        .cornerRadius(8)
    }

    // MARK: - Helper Functions

    private var usedChannelCount: Int {
        var count = 0
        for stream in driverManager.streams {
            if let mapping = stream.mapping {
                count += Int(mapping.deviceChannelCount)
            }
        }
        return count
    }

    private func mappingForChannel(_ channel: Int) -> ChannelMappingInfo? {
        for stream in driverManager.streams {
            if let mapping = stream.mapping {
                let start = Int(mapping.deviceChannelStart)
                let end = start + Int(mapping.deviceChannelCount)
                if channel >= start && channel < end {
                    return mapping
                }
            }
        }
        return nil
    }

    private func isChannelInSelectedStream(_ channel: Int) -> Bool {
        guard let stream = selectedStream,
              let mapping = stream.mapping else {
            return false
        }
        let start = Int(mapping.deviceChannelStart)
        let end = start + Int(mapping.deviceChannelCount)
        return channel >= start && channel < end
    }

    private func colorForStream(_ stream: StreamInfo) -> Color {
        // Generate consistent color based on stream ID
        let hash = stream.id.hashValue
        let hue = Double(abs(hash) % 360) / 360.0
        return Color(hue: hue, saturation: 0.6, brightness: 0.8)
    }

    private func handleChannelTap(_ channel: Int) {
        selectedChannel = channel
    }

    private func autoAssignChannels(for stream: StreamInfo) {
        // Find first available contiguous block
        let neededChannels = Int(stream.numChannels)

        for startCh in 0...(totalChannels - neededChannels) {
            var available = true
            for ch in startCh..<(startCh + neededChannels) {
                if mappingForChannel(ch) != nil {
                    available = false
                    break
                }
            }

            if available {
                // Found a spot - create mapping
                driverManager.assignMapping(
                    streamID: stream.id,
                    deviceChannelStart: UInt16(startCh),
                    deviceChannelCount: stream.numChannels
                )
                break
            }
        }
    }

    private func clearMapping(for stream: StreamInfo) {
        driverManager.clearMapping(streamID: stream.id)
    }
}

// MARK: - Channel Cell

struct ChannelCell: View {
    let channelNumber: Int
    let mapping: ChannelMappingInfo?
    let isSelected: Bool
    let isHovered: Bool
    let isInSelectedStream: Bool

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 3)
                .fill(backgroundColor)
                .overlay(
                    RoundedRectangle(cornerRadius: 3)
                        .strokeBorder(borderColor, lineWidth: borderWidth)
                )

            if isHovered || isSelected {
                Text("\(channelNumber)")
                    .font(.system(size: 8, design: .monospaced))
                    .foregroundColor(.white)
            }
        }
        .frame(height: 28)
        .help("Channel \(channelNumber)\(mapping != nil ? " - \(mapping!.streamName)" : "")")
    }

    private var backgroundColor: Color {
        if isSelected {
            return .green.opacity(0.8)
        } else if isInSelectedStream {
            return .green.opacity(0.4)
        } else if mapping != nil {
            return .blue.opacity(0.6)
        } else if isHovered {
            return .gray.opacity(0.3)
        } else {
            return .gray.opacity(0.15)
        }
    }

    private var borderColor: Color {
        if isSelected || isHovered {
            return .white.opacity(0.5)
        } else {
            return .clear
        }
    }

    private var borderWidth: CGFloat {
        isSelected ? 2 : (isHovered ? 1 : 0)
    }
}

// MARK: - Stream Mapping Row

struct StreamMappingRow: View {
    let stream: StreamInfo
    let isSelected: Bool
    let color: Color

    var body: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(color)
                .frame(width: 12, height: 12)

            VStack(alignment: .leading, spacing: 2) {
                Text(stream.name)
                    .font(.subheadline)
                    .fontWeight(isSelected ? .semibold : .regular)
                    .lineLimit(1)

                if let mapping = stream.mapping {
                    Text("Ch \(mapping.deviceChannelStart)-\(mapping.deviceChannelStart + mapping.deviceChannelCount - 1)")
                        .font(.caption)
                        .foregroundColor(.secondary)
                } else {
                    Text("Not mapped")
                        .font(.caption)
                        .foregroundColor(.orange)
                }
            }

            Spacer()

            Text("\(stream.numChannels)")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .padding(8)
        .background(isSelected ? Color.blue.opacity(0.15) : Color.clear)
        .cornerRadius(6)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .strokeBorder(isSelected ? Color.blue : Color.clear, lineWidth: 1)
        )
    }
}

// MARK: - Legend Item

struct LegendItem: View {
    let color: Color
    let label: String

    var body: some View {
        HStack(spacing: 4) {
            RoundedRectangle(cornerRadius: 2)
                .fill(color)
                .frame(width: 12, height: 12)
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }
}

// MARK: - Preview

#Preview {
    ChannelMappingView(driverManager: DriverManager())
        .frame(width: 1200, height: 800)
}
