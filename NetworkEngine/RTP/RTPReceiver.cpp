//
// RTPReceiver.cpp
// AES67 macOS Driver - Build #9
// RTP packet receiver with L16/L24 decoding and channel mapping integration
//

#include "RTPReceiver.h"
#include "SimpleRTP.h"
#include <cstring>
#include <stdexcept>
#include <chrono>

namespace AES67 {

RTPReceiver::RTPReceiver(
    const SDPSession& sdp,
    const ChannelMapping& mapping,
    DeviceChannelBuffers& deviceChannels
)
    : sdp_(sdp)
    , mapping_(mapping)
    , deviceChannels_(deviceChannels)
{
    std::memset(&stats_, 0, sizeof(stats_));

    // Pre-allocate audio buffer to avoid allocations in receiveLoop()
    // Max 512 frames × stream channels (e.g., 512 × 8 = 4096 floats)
    const size_t maxFrames = 512;
    const size_t maxSamples = maxFrames * sdp_.numChannels;
    audioBuffer_.resize(maxSamples);
}

RTPReceiver::~RTPReceiver() {
    stop();
}

bool RTPReceiver::start() {
    if (running_ || rtpSocket_.isOpen()) {
        return false; // Already running
    }

    // Validate SDP configuration
    if (sdp_.connectionAddress.empty() || sdp_.port == 0) {
        return false;
    }

    if (sdp_.numChannels == 0 || sdp_.numChannels > 128) {
        return false;
    }

    // Open RTP receiver socket
    if (!rtpSocket_.openReceiver(sdp_.connectionAddress.c_str(), sdp_.port)) {
        return false;
    }

    // Start receive thread
    running_ = true;
    receiveThread_ = std::thread(&RTPReceiver::receiveLoop, this);

    return true;
}

void RTPReceiver::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }

    rtpSocket_.close();
    connected_ = false;
}

Statistics RTPReceiver::getStatistics() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void RTPReceiver::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    std::memset(&stats_, 0, sizeof(stats_));
    lastSequenceNumber_ = 0;
    lastTimestamp_ = 0;
}

bool RTPReceiver::isConnected() const {
    if (!connected_) {
        return false;
    }

    // Consider disconnected if no packet in last 1 second
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastPacketTime_
    );

    return elapsed.count() < 1000;
}

int64_t RTPReceiver::getTimeSinceLastPacket() const {
    if (!connected_) {
        return -1;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastPacketTime_
    );

    return elapsed.count();
}

bool RTPReceiver::updateMapping(const ChannelMapping& newMapping) {
    // Validate mapping
    if (newMapping.deviceChannelStart + sdp_.numChannels > 128) {
        return false;
    }

    // Stop, update, restart
    const bool wasRunning = running_;
    if (wasRunning) {
        stop();
    }

    mapping_ = newMapping;

    if (wasRunning) {
        return start();
    }

    return true;
}

void RTPReceiver::receiveLoop() {
    // Packet polling interval (500 μs = 0.5 ms)
    // Fast enough for 48kHz @ 48 samples/packet (1ms intervals)
    constexpr int kPollingIntervalUs = 500;

    RTP::RTPPacket packet;

    while (running_) {
        // Try to receive packet
        ssize_t bytesReceived = rtpSocket_.receive(packet, receiveBuffer_, sizeof(receiveBuffer_));

        if (bytesReceived > 0) {
            processPacket(packet);
        } else {
            // No packet available, wait briefly
            std::this_thread::sleep_for(std::chrono::microseconds(kPollingIntervalUs));
        }
    }
}

void RTPReceiver::processPacket(const RTP::RTPPacket& packet) {
    if (!validatePacket(packet)) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.malformedPackets++;
        return;
    }

    // Get RTP header info
    uint16_t sequenceNumber = packet.header.sequenceNumber;
    uint32_t timestamp = packet.header.timestamp;

    // Get payload
    uint8_t* payload = packet.payload;
    size_t payloadSize = packet.payloadSize;

    if (!payload || payloadSize == 0) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.malformedPackets++;
        return;
    }

    // Update connection state
    if (!connected_) {
        connected_ = true;
    }
    lastPacketTime_ = std::chrono::steady_clock::now();

    // Update statistics
    updateStats(sequenceNumber, payloadSize);

    // Decode based on encoding type
    if (sdp_.encoding == "L16") {
        decodeL16(payload, payloadSize);
    } else if (sdp_.encoding == "L24") {
        decodeL24(payload, payloadSize);
    }
}

bool RTPReceiver::validatePacket(const RTP::RTPPacket& packet) {
    // Check RTP version (should be 2)
    if (packet.header.version != 2) {
        return false;
    }

    // Check payload type matches SDP
    if (packet.header.payloadType != sdp_.payloadType) {
        return false;
    }

    // Check payload size is reasonable
    if (packet.payloadSize == 0 || packet.payloadSize > 1500) {
        return false;
    }

    return true;
}

void RTPReceiver::decodeL16(const uint8_t* payload, size_t payloadSize) {
    // L16: 16-bit big-endian signed PCM
    const size_t bytesPerSample = 2;
    const size_t bytesPerFrame = bytesPerSample * sdp_.numChannels;
    const size_t frameCount = payloadSize / bytesPerFrame;

    if (frameCount == 0 || frameCount > 512) {
        return; // Invalid or excessive frame count
    }

    // Ensure audio buffer is large enough
    const size_t totalSamples = frameCount * sdp_.numChannels;
    if (totalSamples > audioBuffer_.size()) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.malformedPackets++;
        return;
    }

    // Decode: big-endian int16 → float [-1.0, 1.0)
    for (size_t i = 0; i < totalSamples; ++i) {
        const size_t offset = i * bytesPerSample;
        if (offset + 1 >= payloadSize) break;

        // Big-endian 16-bit signed
        int16_t pcmSample = (payload[offset] << 8) | payload[offset + 1];

        // Convert to float: divide by 32768 (2^15)
        audioBuffer_[i] = pcmSample / 32768.0f;
    }

    // Map to device channels
    mapChannelsToDevice(audioBuffer_.data(), frameCount);
}

void RTPReceiver::decodeL24(const uint8_t* payload, size_t payloadSize) {
    // L24: 24-bit big-endian signed PCM
    const size_t bytesPerSample = 3;
    const size_t bytesPerFrame = bytesPerSample * sdp_.numChannels;
    const size_t frameCount = payloadSize / bytesPerFrame;

    if (frameCount == 0 || frameCount > 512) {
        return; // Invalid or excessive frame count
    }

    // Ensure audio buffer is large enough
    const size_t totalSamples = frameCount * sdp_.numChannels;
    if (totalSamples > audioBuffer_.size()) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.malformedPackets++;
        return;
    }

    // Decode: big-endian int24 → float [-1.0, 1.0)
    for (size_t i = 0; i < totalSamples; ++i) {
        const size_t offset = i * bytesPerSample;
        if (offset + 2 >= payloadSize) break;

        // Big-endian 24-bit signed (sign-extend to 32-bit)
        int32_t pcmSample = (payload[offset] << 24) |
                           (payload[offset + 1] << 16) |
                           (payload[offset + 2] << 8);
        pcmSample >>= 8; // Arithmetic right shift preserves sign

        // Convert to float: divide by 8388608 (2^23)
        audioBuffer_[i] = pcmSample / 8388608.0f;
    }

    // Map to device channels
    mapChannelsToDevice(audioBuffer_.data(), frameCount);
}

void RTPReceiver::mapChannelsToDevice(const float* interleavedAudio, size_t frameCount) {
    // Validate mapping
    const size_t deviceChannelEnd = mapping_.deviceChannelStart + sdp_.numChannels;
    if (deviceChannelEnd > 128) {
        return; // Mapping out of range
    }

    // Stack-allocated temporary buffer for de-interleaving (max 512 frames)
    constexpr size_t kMaxFrames = 512;
    if (frameCount > kMaxFrames) {
        return;
    }

    float channelBuffer[kMaxFrames];

    // Write each stream channel to its mapped device channel
    // This de-interleaves: [ch0_f0, ch1_f0, ch0_f1, ch1_f1, ...]
    //                   → deviceChannels[0]: [ch0_f0, ch0_f1, ...]
    //                   → deviceChannels[1]: [ch1_f0, ch1_f1, ...]

    bool hadUnderrun = false;

    for (size_t streamChannel = 0; streamChannel < sdp_.numChannels; ++streamChannel) {
        const size_t deviceChannel = mapping_.deviceChannelStart + streamChannel;

        // Extract this channel from interleaved stream
        for (size_t frame = 0; frame < frameCount; ++frame) {
            channelBuffer[frame] = interleavedAudio[frame * sdp_.numChannels + streamChannel];
        }

        // Write to device ring buffer (batch write)
        const size_t written = deviceChannels_[deviceChannel].write(channelBuffer, frameCount);

        if (written < frameCount && !hadUnderrun) {
            // Ring buffer full - count underrun once per packet
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.underruns++;
            hadUnderrun = true;
        }
    }
}

void RTPReceiver::updateStats(uint16_t sequenceNumber, size_t payloadSize) {
    std::lock_guard<std::mutex> lock(statsMutex_);

    // Detect packet loss (sequence number gaps)
    if (stats_.packetsReceived > 0) {
        uint16_t expected = lastSequenceNumber_ + 1;
        if (sequenceNumber != expected) {
            // Handle sequence number wrap-around
            uint16_t gap = sequenceNumber - expected;
            stats_.packetsLost += gap;
        }
    }

    lastSequenceNumber_ = sequenceNumber;
    stats_.packetsReceived++;
    stats_.bytesReceived += payloadSize;

    // Note: Packet loss percentage is calculated by Statistics::getPacketLossPercent()
}

} // namespace AES67
