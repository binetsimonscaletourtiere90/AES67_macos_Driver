//
// StreamListView.swift
// AES67 Manager - Build #7
// Sidebar list of active streams
//

import SwiftUI

struct StreamListView: View {
    @EnvironmentObject var driverManager: DriverManager
    @Binding var selectedStream: StreamInfo?

    var body: some View {
        List(selection: $selectedStream) {
            Section("Active Streams (\(driverManager.streams.count))") {
                ForEach(driverManager.streams) { stream in
                    StreamRowView(stream: stream)
                        .tag(stream)
                        .contextMenu {
                            Button("Remove Stream") {
                                driverManager.removeStream(stream)
                            }

                            Button("Export SDP...") {
                                driverManager.exportSDP(for: stream)
                            }

                            Divider()

                            Button("View Details") {
                                selectedStream = stream
                            }
                        }
                }
            }

            Section("Statistics") {
                HStack {
                    Text("Total Channels")
                    Spacer()
                    Text("\(driverManager.totalChannelsUsed)/128")
                        .foregroundColor(.secondary)
                }

                HStack {
                    Text("Available")
                    Spacer()
                    Text("\(128 - driverManager.totalChannelsUsed)")
                        .foregroundColor(.secondary)
                }
            }
        }
        .listStyle(.sidebar)
    }
}

struct StreamRowView: View {
    let stream: StreamInfo

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Image(systemName: stream.isConnected ? "antenna.radiowaves.left.and.right" : "antenna.radiowaves.left.and.right.slash")
                    .foregroundColor(stream.isConnected ? .green : .orange)
                    .font(.caption)

                Text(stream.name)
                    .font(.headline)
            }

            HStack {
                Text("\(stream.numChannels)ch")
                Text("•")
                Text("\(formatSampleRate(stream.sampleRate))")
                Text("•")
                Text(stream.encoding)
            }
            .font(.caption)
            .foregroundColor(.secondary)

            if let mapping = stream.mapping {
                Text("Channels \(mapping.deviceChannelStart)-\(mapping.deviceChannelStart + mapping.deviceChannelCount - 1)")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.vertical, 4)
    }

    private func formatSampleRate(_ rate: UInt32) -> String {
        if rate >= 1000 {
            return "\(rate / 1000)kHz"
        }
        return "\(rate)Hz"
    }
}

#Preview {
    StreamListView(selectedStream: .constant(nil))
        .environmentObject(DriverManager())
}
