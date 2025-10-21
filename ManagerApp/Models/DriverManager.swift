//
// DriverManager.swift
// AES67 Manager - Build #7
// Main interface for managing the AES67 driver
//

import Foundation
import SwiftUI
import CoreAudio

class DriverManager: ObservableObject {
    @Published var streams: [StreamInfo] = []
    @Published var isDriverLoaded: Bool = false
    @Published var showAddStreamSheet: Bool = false
    @Published var totalChannelsUsed: Int = 0

    private let configURL = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/AES67Driver/config.json")

    private var refreshTimer: Timer?

    init() {
        checkDriverStatus()
        loadConfiguration()
        startAutoRefresh()
    }

    // MARK: - Driver Status

    func checkDriverStatus() {
        // Check if AES67Driver.driver exists in HAL plugins
        let driverPath = "/Library/Audio/Plug-Ins/HAL/AES67Driver.driver"
        isDriverLoaded = FileManager.default.fileExists(atPath: driverPath)
    }

    func checkDriverInstallation() {
        checkDriverStatus()

        if isDriverLoaded {
            showAlert(title: "Driver Installed",
                     message: "AES67 driver is properly installed at /Library/Audio/Plug-Ins/HAL/")
        } else {
            showAlert(title: "Driver Not Found",
                     message: "Please install the AES67 driver package first.")
        }
    }

    func restartCoreAudio() {
        let task = Process()
        task.launchPath = "/usr/bin/sudo"
        task.arguments = ["killall", "coreaudiod"]

        do {
            try task.run()
            task.waitUntilExit()

            DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                self.checkDriverStatus()
            }
        } catch {
            print("Failed to restart CoreAudio: \(error)")
        }
    }

    // MARK: - Stream Management

    func addStream(name: String, multicastIP: String, port: UInt16,
                   numChannels: UInt16, sampleRate: UInt32, encoding: String) {

        let stream = StreamInfo(
            id: UUID(),
            name: name,
            multicastIP: multicastIP,
            port: port,
            encoding: encoding,
            sampleRate: sampleRate,
            numChannels: numChannels
        )

        // Auto-assign device channels
        let deviceChannelStart = UInt16(totalChannelsUsed)

        var mapping = ChannelMappingInfo(
            streamID: stream.id,
            streamName: name,
            streamChannelCount: numChannels,
            deviceChannelStart: deviceChannelStart,
            deviceChannelCount: numChannels
        )

        var newStream = stream
        newStream.mapping = mapping

        streams.append(newStream)
        updateTotalChannels()
        saveConfiguration()
    }

    func removeStream(_ stream: StreamInfo) {
        streams.removeAll { $0.id == stream.id }
        updateTotalChannels()
        saveConfiguration()
    }

    func refreshStatus() {
        checkDriverStatus()
        // In a real implementation, this would query the driver for current status
        // For now, we just refresh the driver status
    }

    // MARK: - Channel Mapping

    func assignMapping(streamID: UUID, deviceChannelStart: UInt16, deviceChannelCount: UInt16) {
        guard let index = streams.firstIndex(where: { $0.id == streamID }) else { return }

        let mapping = ChannelMappingInfo(
            streamID: streamID,
            streamName: streams[index].name,
            streamChannelCount: streams[index].numChannels,
            deviceChannelStart: deviceChannelStart,
            deviceChannelCount: deviceChannelCount
        )

        streams[index].mapping = mapping
        saveConfiguration()
    }

    func clearMapping(streamID: UUID) {
        guard let index = streams.firstIndex(where: { $0.id == streamID }) else { return }
        streams[index].mapping = nil
        saveConfiguration()
    }

    // MARK: - SDP Import/Export

    func importSDPFile() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.init(filenameExtension: "sdp")!]
        panel.allowsMultipleSelection = false
        panel.message = "Select an SDP file to import"

        panel.begin { response in
            if response == .OK, let url = panel.url {
                self.parseSDP(from: url)
            }
        }
    }

    func exportSDP(for stream: StreamInfo) {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.init(filenameExtension: "sdp")!]
        panel.nameFieldStringValue = "\(stream.name).sdp"
        panel.message = "Export SDP file"

        panel.begin { response in
            if response == .OK, let url = panel.url {
                self.generateSDP(for: stream, to: url)
            }
        }
    }

    private func parseSDP(from url: URL) {
        do {
            let content = try String(contentsOf: url, encoding: .utf8)
            // Parse SDP content (simplified version)
            // In a real implementation, this would use the C++ SDPParser

            let lines = content.components(separatedBy: .newlines)
            var name = "Imported Stream"
            var multicastIP = "239.0.0.1"
            var port: UInt16 = 5004
            var numChannels: UInt16 = 2
            var sampleRate: UInt32 = 48000
            var encoding = "L24"

            for line in lines {
                if line.hasPrefix("s=") {
                    name = String(line.dropFirst(2))
                } else if line.hasPrefix("c=") {
                    // c=IN IP4 239.0.0.1/32
                    let parts = line.components(separatedBy: " ")
                    if parts.count >= 3 {
                        multicastIP = parts[2].components(separatedBy: "/").first ?? multicastIP
                    }
                } else if line.hasPrefix("m=audio ") {
                    // m=audio 5004 RTP/AVP 97
                    let parts = line.components(separatedBy: " ")
                    if parts.count >= 2 {
                        port = UInt16(parts[1]) ?? 5004
                    }
                } else if line.contains("rtpmap") {
                    // a=rtpmap:97 L24/48000/2
                    if let match = line.range(of: "L\\d+/\\d+/\\d+", options: .regularExpression) {
                        let rtpInfo = String(line[match])
                        let parts = rtpInfo.components(separatedBy: "/")
                        if parts.count >= 3 {
                            encoding = parts[0]
                            sampleRate = UInt32(parts[1]) ?? 48000
                            numChannels = UInt16(parts[2]) ?? 2
                        }
                    }
                }
            }

            addStream(name: name, multicastIP: multicastIP, port: port,
                     numChannels: numChannels, sampleRate: sampleRate, encoding: encoding)

        } catch {
            print("Failed to import SDP: \(error)")
        }
    }

    private func generateSDP(for stream: StreamInfo, to url: URL) {
        var sdp = "v=0\n"
        sdp += "o=- \(Int(Date().timeIntervalSince1970)) 1 IN IP4 127.0.0.1\n"
        sdp += "s=\(stream.name)\n"
        sdp += "c=IN IP4 \(stream.multicastIP)/\(stream.ttl)\n"
        sdp += "t=0 0\n"
        sdp += "m=audio \(stream.port) RTP/AVP \(stream.payloadType)\n"
        sdp += "a=rtpmap:\(stream.payloadType) \(stream.encoding)/\(stream.sampleRate)/\(stream.numChannels)\n"
        sdp += "a=ptime:1\n"

        do {
            try sdp.write(to: url, atomically: true, encoding: .utf8)
        } catch {
            print("Failed to export SDP: \(error)")
        }
    }

    // MARK: - Configuration Persistence

    private func loadConfiguration() {
        guard FileManager.default.fileExists(atPath: configURL.path) else { return }

        do {
            let data = try Data(contentsOf: configURL)
            let config = try JSONDecoder().decode(DriverConfiguration.self, from: data)

            // Convert config to streams
            streams = config.streams.compactMap { streamConfig in
                let id = UUID(uuidString: streamConfig.id) ?? UUID()
                let mapping = config.mappings.first { $0.streamID == id }

                return StreamInfo(
                    id: id,
                    name: streamConfig.name,
                    description: streamConfig.description,
                    multicastIP: streamConfig.multicastIP,
                    port: streamConfig.port,
                    encoding: streamConfig.encoding,
                    sampleRate: streamConfig.sampleRate,
                    numChannels: streamConfig.numChannels,
                    payloadType: streamConfig.payloadType,
                    ptpDomain: streamConfig.ptpDomain,
                    mapping: mapping
                )
            }

            updateTotalChannels()
        } catch {
            print("Failed to load configuration: \(error)")
        }
    }

    private func saveConfiguration() {
        // Create config directory if needed
        let configDir = configURL.deletingLastPathComponent()
        try? FileManager.default.createDirectory(at: configDir, withIntermediateDirectories: true)

        let config = DriverConfiguration(
            streams: streams.map { stream in
                StreamConfig(
                    id: stream.id.uuidString,
                    name: stream.name,
                    description: stream.description,
                    multicastIP: stream.multicastIP,
                    port: stream.port,
                    encoding: stream.encoding,
                    sampleRate: stream.sampleRate,
                    numChannels: stream.numChannels,
                    payloadType: stream.payloadType,
                    ptpDomain: stream.ptpDomain
                )
            },
            mappings: streams.compactMap { $0.mapping }
        )

        do {
            let encoder = JSONEncoder()
            encoder.outputFormatting = .prettyPrinted
            let data = try encoder.encode(config)
            try data.write(to: configURL)
        } catch {
            print("Failed to save configuration: \(error)")
        }
    }

    // MARK: - Helpers

    private func updateTotalChannels() {
        totalChannelsUsed = streams.reduce(0) { $0 + Int($1.numChannels) }
    }

    private func startAutoRefresh() {
        refreshTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            self?.refreshStatus()
        }
    }

    private func showAlert(title: String, message: String) {
        DispatchQueue.main.async {
            let alert = NSAlert()
            alert.messageText = title
            alert.informativeText = message
            alert.alertStyle = .informational
            alert.runModal()
        }
    }

    deinit {
        refreshTimer?.invalidate()
    }
}
