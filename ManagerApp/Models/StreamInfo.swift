//
// StreamInfo.swift
// AES67 Manager - Build #7
// Data models for streams and mappings
//

import Foundation

struct StreamInfo: Identifiable, Hashable {
    let id: UUID
    var name: String
    var description: String?

    // Network
    var multicastIP: String
    var port: UInt16
    var sourceIP: String?
    var ttl: UInt8 = 32

    // Audio format
    var encoding: String  // "L16" or "L24"
    var sampleRate: UInt32
    var numChannels: UInt16
    var payloadType: UInt8 = 97

    // PTP
    var ptpDomain: Int = 0

    // Status
    var isActive: Bool = true
    var isConnected: Bool = false
    var startTime: Date?

    // Mapping
    var mapping: ChannelMappingInfo?

    // Statistics
    var statistics: StreamStatistics?

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }

    static func == (lhs: StreamInfo, rhs: StreamInfo) -> Bool {
        lhs.id == rhs.id
    }
}

struct ChannelMappingInfo: Codable {
    var streamID: UUID
    var streamName: String
    var streamChannelCount: UInt16
    var streamChannelOffset: UInt16 = 0
    var deviceChannelStart: UInt16
    var deviceChannelCount: UInt16
}

struct StreamStatistics {
    var packetsReceived: UInt64 = 0
    var packetsLost: UInt64 = 0
    var lossPercentage: Double = 0.0
    var bytesReceived: UInt64 = 0
    var bytesSent: UInt64 = 0
    var underruns: UInt64 = 0
    var overruns: UInt64 = 0
    var malformedPackets: UInt64 = 0
}

// MARK: - Example Data for Previews

extension StreamInfo {
    static var example: StreamInfo {
        StreamInfo(
            id: UUID(),
            name: "Riedel Artist Panel 1",
            description: "Intercom system - 8 channels",
            multicastIP: "239.0.0.1",
            port: 5004,
            sourceIP: "192.168.1.100",
            encoding: "L24",
            sampleRate: 48000,
            numChannels: 8,
            isConnected: true,
            startTime: Date(),
            mapping: ChannelMappingInfo(
                streamID: UUID(),
                streamName: "Riedel Artist Panel 1",
                streamChannelCount: 8,
                deviceChannelStart: 0,
                deviceChannelCount: 8
            ),
            statistics: StreamStatistics(
                packetsReceived: 150000,
                packetsLost: 12,
                lossPercentage: 0.008,
                bytesReceived: 36_000_000,
                underruns: 0,
                overruns: 0
            )
        )
    }
}

// MARK: - Codable Configuration

struct DriverConfiguration: Codable {
    var version: String = "1.0.7"
    var streams: [StreamConfig] = []
    var mappings: [ChannelMappingInfo] = []
}

struct StreamConfig: Codable {
    var id: String
    var name: String
    var description: String?
    var multicastIP: String
    var port: UInt16
    var encoding: String
    var sampleRate: UInt32
    var numChannels: UInt16
    var payloadType: UInt8
    var ptpDomain: Int
}
