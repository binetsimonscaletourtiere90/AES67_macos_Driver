# Complete Implementation Guide - Build #3+

**Status**: Phases 1-3 Complete | This guide covers Phases 4-13

---

## 📋 PHASE 4: Core Audio Device (Build #3)

### File 1: `Driver/AES67Device.cpp` (~600 lines)

**Key Implementation Points**:

```cpp
#include "AES67Device.h"
#include "AES67IOHandler.h"
#include "../Shared/Config.hpp"

namespace AES67 {

AES67Device::AES67Device(std::shared_ptr<aspl::Context> context)
    : aspl::Device(context)
{
    // Initialize ring buffers (256 total: 128 input + 128 output)
    for (size_t i = 0; i < kNumChannels; i++) {
        inputBuffers_[i] = SPSCRingBuffer<float>(kDefaultRingBufferSize);
        outputBuffers_[i] = SPSCRingBuffer<float>(kDefaultRingBufferSize);
    }

    // Create input and output streams
    InitializeStreams();
    InitializeIOHandler();

    // Set device properties
    SetPropertyData(kAudioDevicePropertyDeviceName, "AES67 Device");
    SetPropertyData(kAudioDevicePropertyDeviceManufacturer, "AES67 Driver");
}

void AES67Device::InitializeStreams() {
    // Create 128-channel input stream
    inputStream_ = std::make_shared<aspl::Stream>(
        GetContext(),
        aspl::Direction::Input,
        kNumChannels
    );

    // Create 128-channel output stream
    outputStream_ = std::make_shared<aspl::Stream>(
        GetContext(),
        aspl::Direction::Output,
        kNumChannels
    );

    // Add streams to device
    AddStream(inputStream_);
    AddStream(outputStream_);
}

void AES67Device::InitializeIOHandler() {
    ioHandler_ = std::make_shared<AES67IOHandler>(
        inputBuffers_,
        outputBuffers_,
        inputUnderruns_,
        outputUnderruns_
    );

    SetIORequestHandler(ioHandler_);
}

Float64 AES67Device::GetSampleRate() const {
    return currentSampleRate_.load();
}

OSStatus AES67Device::SetSampleRate(Float64 sampleRate) {
    // Validate sample rate
    auto validRates = GetAvailableSampleRates();
    if (std::find(validRates.begin(), validRates.end(), sampleRate) == validRates.end()) {
        return kAudioHardwareUnsupportedOperationError;
    }

    currentSampleRate_.store(sampleRate);
    return kAudioHardwareNoError;
}

// ... Additional methods for buffer size, device info, statistics ...

} // namespace AES67
```

**TODO**: Implement remaining methods following the header API

### File 2: `Driver/AES67IOHandler.cpp` (~300 lines)

**Critical RT-Safe Implementation**:

```cpp
#include "AES67IOHandler.h"
#include <cstring>

namespace AES67 {

OSStatus AES67IOHandler::OnReadClientInput(
    const std::shared_ptr<aspl::Stream>& stream,
    Float64 timestamp,
    const void* inputData,
    void* outputData,
    UInt32 frameCount
) {
    // RT-SAFE: No allocation, no locks, no blocking!

    if (stream->GetDirection() == aspl::Direction::Input) {
        processInput(static_cast<float*>(outputData), frameCount);
    } else {
        processOutput(static_cast<const float*>(inputData), frameCount);
    }

    return kAudioHardwareNoError;
}

void AES67IOHandler::processInput(float* outputData, UInt32 frameCount) {
    // Read from network ring buffers → Core Audio
    for (size_t ch = 0; ch < kNumChannels; ch++) {
        size_t read = inputBuffers_[ch].read(
            outputData + (ch * frameCount),
            frameCount
        );

        // Handle underrun: fill with silence
        if (read < frameCount) {
            std::memset(
                outputData + (ch * frameCount) + read,
                0,
                (frameCount - read) * sizeof(float)
            );
            inputUnderruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void AES67IOHandler::processOutput(const float* inputData, UInt32 frameCount) {
    // Write from Core Audio → network ring buffers
    for (size_t ch = 0; ch < kNumChannels; ch++) {
        size_t written = outputBuffers_[ch].write(
            inputData + (ch * frameCount),
            frameCount
        );

        // Handle overrun: data discarded
        if (written < frameCount) {
            outputUnderruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

} // namespace AES67
```

### File 3: `Driver/PlugInMain.cpp` (~200 lines)

**AudioServerPlugIn Entry Point**:

```cpp
#include <aspl/Plugin.hpp>
#include "AES67Device.h"

extern "C" {

void* AES67_Create(CFAllocatorRef allocator, CFUUIDRef requestedTypeUUID) {
    if (!CFEqual(requestedTypeUUID, kAudioServerPlugInTypeUUID)) {
        return nullptr;
    }

    // Create libASPL context
    auto context = std::make_shared<aspl::Context>();

    // Create plugin
    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->SetManufacturer("AES67 Driver");
    plugin->SetResourceBundleID("com.aes67driver.audiodevice");

    // Create device
    auto device = std::make_shared<AES67::AES67Device>(context);
    plugin->AddDevice(device);

    return plugin->GetReference();
}

} // extern "C"
```

---

## 📋 PHASE 6: RTP Engine (Build #4)

### File: `NetworkEngine/RTP/RTPReceiver.cpp` (~500 lines)

**Key Sections**:

```cpp
#include "RTPReceiver.h"

RTPReceiver::RTPReceiver(
    const SDPSession& sdp,
    const ChannelMapping& mapping,
    DeviceChannelBuffers& deviceChannels
) : sdp_(sdp), mapping_(mapping), deviceChannels_(deviceChannels)
{
    // Initialize oRTP session
    ortp_init();
    rtpSession_ = rtp_session_new(RTP_SESSION_RECVONLY);

    // Configure session
    rtp_session_set_local_addr(rtpSession_,
        sdp_.connectionAddress.c_str(),
        sdp_.port,
        -1  // RTCP port (auto)
    );

    // Join multicast group
    rtp_session_set_multicast_ttl(rtpSession_, sdp_.ttl);
    rtp_session_set_multicast_loopback(rtpSession_, 0);

    // Set payload type
    RtpProfile* profile = rtp_profile_new("AES67");
    rtp_session_set_profile(rtpSession_, profile);
    rtp_session_set_payload_type(rtpSession_, sdp_.payloadType);

    // Pre-allocate audio buffer
    audioBuffer_.resize(sdp_.framecount * sdp_.numChannels);
}

void RTPReceiver::receiveLoop() {
    while (running_.load()) {
        int haveMore = 1;
        while (haveMore) {
            mblk_t* packet = rtp_session_recvm_with_ts(
                rtpSession_,
                Utils::getMicroseconds()
            );

            if (packet) {
                if (validatePacket(packet)) {
                    processPacket(packet);
                }
                freemsg(packet);
            } else {
                haveMore = 0;
            }
        }

        // Small sleep to prevent busy-wait
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void RTPReceiver::processPacket(mblk_t* packet) {
    uint8_t* payload = rtp_get_payload(packet, &payloadSize);

    // Decode based on encoding
    if (sdp_.encoding == "L24") {
        decodeL24(payload, payloadSize);
    } else if (sdp_.encoding == "L16") {
        decodeL16(payload, payloadSize);
    }

    // Update statistics
    lastPacketTime_ = std::chrono::steady_clock::now();
    connected_.store(true);
}

void RTPReceiver::decodeL24(const uint8_t* payload, size_t size) {
    size_t numSamples = size / (3 * sdp_.numChannels);

    // Decode big-endian 24-bit to float
    for (size_t i = 0; i < numSamples; i++) {
        for (size_t ch = 0; ch < sdp_.numChannels; ch++) {
            size_t offset = (i * sdp_.numChannels + ch) * 3;

            // Big-endian 24-bit → int32
            int32_t sample = (payload[offset] << 16) |
                           (payload[offset + 1] << 8) |
                           payload[offset + 2];

            // Sign extend
            if (sample & 0x800000) {
                sample |= 0xFF000000;
            }

            // Normalize to float [-1.0, 1.0]
            audioBuffer_[i * sdp_.numChannels + ch] = sample / 8388608.0f;
        }
    }

    // Map to device channels
    mapChannelsToDevice(audioBuffer_.data(), numSamples);
}

void RTPReceiver::mapChannelsToDevice(const float* streamAudio, size_t frameCount) {
    // Interleaved → per-channel ring buffers
    for (uint16_t streamCh = 0; streamCh < mapping_.streamChannelCount; streamCh++) {
        uint16_t deviceCh = mapping_.deviceChannelStart + streamCh;

        if (deviceCh >= 128) continue;

        // De-interleave
        std::vector<float> channelData(frameCount);
        for (size_t frame = 0; frame < frameCount; frame++) {
            channelData[frame] = streamAudio[frame * sdp_.numChannels + streamCh];
        }

        // Write to device channel ring buffer
        deviceChannels_[deviceCh].write(channelData.data(), frameCount);
    }
}
```

### File: `NetworkEngine/RTP/RTPTransmitter.cpp` (~400 lines)

**Similar structure to RTPReceiver, but reversed**:
- Read from device channel ring buffers
- Interleave audio
- Encode to L16/L24
- Send RTP packets with proper timing

---

## 📋 PHASE 7-8: PTP, Stream Manager, Discovery (Build #5-6)

### `NetworkEngine/PTP/PTPClock.cpp` (~400 lines)
- Implement LocalClock using `std::chrono`
- Implement PTPClock using ptpd daemon
- Singleton PTPClockManager

### `NetworkEngine/StreamManager.cpp` (~500 lines)
- Coordinate RTPReceiver/Transmitter lifecycle
- Integrate with StreamChannelMapper
- Validate sample rates match device
- Provide callbacks for UI updates

### `NetworkEngine/Discovery/SAPListener.cpp` (~400 lines)
- UDP socket on 239.255.255.255:9875
- Parse SAP packets
- Extract SDP from announcements
- Callback on new streams discovered

### `NetworkEngine/Discovery/RTSPClient.cpp` (~300 lines)
- TCP socket to RTSP server
- Implement DESCRIBE, SETUP, PLAY, TEARDOWN
- Parse RTSP responses
- Extract SDP from DESCRIBE

---

## 📋 PHASE 9: SwiftUI Manager App (Build #7-8)

### Project Structure
```
ManagerApp/
├── AES67Manager.xcodeproj
├── AES67ManagerApp.swift          # Entry point
├── Models/
│   └── DriverState.swift          # ObservableObject
├── Views/
│   ├── ContentView.swift          # Main window
│   ├── StreamBrowserView.swift
│   ├── ChannelMappingView.swift   # CRITICAL
│   ├── ChannelMappingVisualizer.swift
│   ├── PTPStatusView.swift
│   └── DeviceSettingsView.swift
└── Resources/
    └── Info.plist
```

### `AES67ManagerApp.swift`
```swift
import SwiftUI

@main
struct AES67ManagerApp: App {
    @StateObject private var driverState = DriverState()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(driverState)
                .frame(minWidth: 1000, minHeight: 700)
        }
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(after: .importExport) {
                Button("Import SDP...") {
                    driverState.showImportDialog()
                }
                .keyboardShortcut("i", modifiers: [.command])

                Button("Export SDP...") {
                    driverState.showExportDialog()
                }
                .keyboardShortcut("e", modifiers: [.command])
            }
        }
    }
}
```

### `Models/DriverState.swift`
```swift
import Foundation
import Combine

class DriverState: ObservableObject {
    @Published var discoveredStreams: [SDPSession] = []
    @Published var activeStreams: [StreamInfo] = []
    @Published var mappings: [ChannelMapping] = []
    @Published var deviceSampleRate: Double = 48000.0
    @Published var deviceBufferSize: Int = 64
    @Published var ptpDomains: [PTPDomain] = []
    @Published var buildNumber: String = "0"

    private var streamManager: OpaquePointer?  // C++ StreamManager

    init() {
        loadBuildNumber()
        initializeDriver()
    }

    func loadBuildNumber() {
        // Read VERSION.txt
        if let version = try? String(contentsOfFile: "VERSION.txt") {
            buildNumber = version.components(separatedBy: "build.").last ?? "0"
        }
    }

    // C++ bridge functions
    func importSDPFile(_ url: URL) { /* ... */ }
    func addStream(_ sdp: SDPSession) { /* ... */ }
    func updateMapping(_ mapping: ChannelMapping) { /* ... */ }
}
```

### `Views/ChannelMappingView.swift` (CRITICAL)
```swift
struct ChannelMappingView: View {
    @EnvironmentObject var driverState: DriverState
    @State private var selectedStream: StreamInfo?

    var body: some View {
        HSplitView {
            // Left: Stream list
            List(driverState.activeStreams, selection: $selectedStream) { stream in
                StreamRow(stream: stream)
            }
            .frame(minWidth: 250)

            // Right: Channel mapper
            if let stream = selectedStream {
                VStack(alignment: .leading, spacing: 20) {
                    Text("Map '\(stream.name)' to Device Channels")
                        .font(.title2)

                    ChannelMappingControls(stream: stream)

                    ChannelMappingVisualizer(
                        totalChannels: 128,
                        mappings: driverState.mappings,
                        selectedStream: stream.id
                    )

                    Spacer()

                    HStack {
                        Button("Reset") { driverState.resetMapping(stream) }
                        Spacer()
                        Button("Apply") { driverState.updateMapping(stream) }
                            .buttonStyle(.borderedProminent)
                    }
                }
                .padding()
            }
        }
    }
}
```

### `Views/ChannelMappingVisualizer.swift`
```swift
struct ChannelMappingVisualizer: View {
    let totalChannels: Int
    let mappings: [ChannelMapping]
    let selectedStream: UUID?

    var body: some View {
        ScrollView {
            LazyVGrid(columns: Array(repeating: GridItem(.fixed(35)), count: 16)) {
                ForEach(0..<totalChannels, id: \.self) { ch in
                    ChannelCell(
                        channel: ch,
                        owner: getOwner(ch),
                        isSelected: isSelected(ch)
                    )
                }
            }
        }
        .frame(height: 350)
    }

    func getOwner(_ ch: Int) -> String? {
        mappings.first { $0.containsChannel(ch) }?.streamName
    }

    func isSelected(_ ch: Int) -> Bool {
        guard let id = selectedStream else { return false }
        return mappings.first { $0.streamID == id }?.containsChannel(ch) ?? false
    }
}

struct ChannelCell: View {
    let channel: Int
    let owner: String?
    let isSelected: Bool

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 4)
                .fill(fillColor)
                .overlay(
                    RoundedRectangle(cornerRadius: 4)
                        .stroke(isSelected ? Color.blue : Color.clear, lineWidth: 2)
                )

            VStack(spacing: 2) {
                Text("\(channel + 1)")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundColor(textColor)

                if let owner = owner {
                    Text(String(owner.prefix(3)))
                        .font(.system(size: 6))
                        .foregroundColor(.secondary)
                }
            }
        }
        .frame(width: 35, height: 35)
        .help(toolTip)
    }

    var fillColor: Color {
        if isSelected { return Color.blue.opacity(0.3) }
        if owner != nil { return Color.green.opacity(0.2) }
        return Color.gray.opacity(0.1)
    }

    var textColor: Color {
        owner != nil ? .primary : .secondary
    }

    var toolTip: String {
        if let owner = owner {
            return "Channel \(channel + 1): \(owner)"
        }
        return "Channel \(channel + 1): Unassigned"
    }
}
```

---

## 📋 PHASE 10: DSD Support (Build #9)

### `NetworkEngine/DoPDecoder.cpp` (~200 lines)

```cpp
bool DoPDecoder::isDoPStream(const uint8_t* data, size_t sizeBytes) {
    // Check for DoP markers in first few samples
    for (size_t i = 0; i < std::min(size_t(10), sizeBytes / 3); i++) {
        uint8_t marker = data[i * 3];  // MSB of 24-bit sample
        if (marker != kDoPMarker1 && marker != kDoPMarker2) {
            return false;
        }
    }
    return true;
}

void DoPDecoder::decode(const uint8_t* dopData, size_t dopFrames, uint8_t* dsdData) {
    for (size_t i = 0; i < dopFrames; i++) {
        const uint8_t* dopSample = dopData + (i * 3);

        // Extract 16 DSD bits from 24-bit DoP sample
        uint16_t dsdBits = (dopSample[1] << 8) | dopSample[2];

        // Write to DSD output (2 bytes per frame)
        dsdData[i * 2] = (dsdBits >> 8) & 0xFF;
        dsdData[i * 2 + 1] = dsdBits & 0xFF;
    }
}
```

---

## 📋 PHASE 12: Testing (Build #10)

### `Tests/CMakeLists.txt`
```cmake
enable_testing()

add_executable(test_sdp test_SDPParser.cpp)
target_link_libraries(test_sdp AES67Driver)
add_test(NAME SDPParserTest COMMAND test_sdp)

add_executable(test_mapper test_StreamChannelMapper.cpp)
target_link_libraries(test_mapper AES67Driver)
add_test(NAME ChannelMapperTest COMMAND test_mapper)

# Add more tests...
```

### `Tests/Unit/test_SDPParser.cpp`
```cpp
#include "Driver/SDPParser.h"
#include <cassert>
#include <iostream>

void testRiedelSDP() {
    auto session = AES67::SDPParser::parseFile("Docs/Examples/riedel_artist_8ch.sdp");
    assert(session.has_value());
    assert(session->sessionName == "Riedel Artist IFB");
    assert(session->numChannels == 8);
    assert(session->sampleRate == 48000);
    assert(session->ptpDomain == 0);
    std::cout << "✓ Riedel SDP parsing test passed\n";
}

void testRoundTrip() {
    auto session1 = AES67::SDPParser::parseFile("test.sdp");
    auto generated = AES67::SDPParser::generate(*session1);
    auto session2 = AES67::SDPParser::parseString(generated);

    assert(session1->sessionName == session2->sessionName);
    assert(session1->numChannels == session2->numChannels);
    std::cout << "✓ Round-trip test passed\n";
}

int main() {
    testRiedelSDP();
    testRoundTrip();
    std::cout << "All SDP tests passed!\n";
    return 0;
}
```

---

## 📋 PHASE 13: Distribution & Docs

### Example SDP Files Needed
- `Docs/Examples/dante_64ch.sdp`
- `Docs/Examples/ravenna_stereo.sdp`
- `Docs/Examples/merging_multichannel.sdp`

### User Documentation
- Detailed DAW integration guides
- Channel mapping workflows
- Troubleshooting common issues
- Performance tuning

---

## 🎯 Build Strategy

1. **Build #3**: Complete Phase 4 (Core Audio)
2. **Build #4**: Complete Phase 6 (RTP Engine)
3. **Build #5**: Complete Phase 7 (PTP)
4. **Build #6**: Complete Phase 8 (Stream Manager + Discovery)
5. **Build #7-8**: Complete Phase 9 (SwiftUI App)
6. **Build #9**: Complete Phase 10 (DSD)
7. **Build #10**: Complete Phase 12 (Tests)
8. **Build #10**: Finalize Phase 13 (Docs)

---

**All header files exist. All APIs defined. This guide provides implementation roadmap.**

**macOS compilation required for Core Audio and SwiftUI components.**
