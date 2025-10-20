//
// RTPReceiver.h
// AES67 macOS Driver - Build #1
// RTP packet receiver with L16/L24 decoding and channel mapping
//

#pragma once

#include "../../Shared/Types.h"
#include "../../Shared/RingBuffer.hpp"
#include "../../Driver/SDPParser.h"
#include "../StreamChannelMapper.h"
#include <ortp/ortp.h>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

namespace AES67 {

//
// RTP Receiver
//
// Receives RTP audio packets from network and writes decoded audio
// to device channels according to the channel mapping
//
class RTPReceiver {
public:
    using DeviceChannelBuffers = std::array<SPSCRingBuffer<float>, 128>;

    //
    // Constructor
    //
    // sdp: SDP session describing the stream to receive
    // mapping: Channel mapping configuration
    // deviceChannels: Reference to device channel ring buffers
    //
    RTPReceiver(
        const SDPSession& sdp,
        const ChannelMapping& mapping,
        DeviceChannelBuffers& deviceChannels
    );

    ~RTPReceiver();

    // Prevent copy/move
    RTPReceiver(const RTPReceiver&) = delete;
    RTPReceiver& operator=(const RTPReceiver&) = delete;

    //
    // Control
    //

    // Start receiving
    bool start();

    // Stop receiving
    void stop();

    // Check if currently receiving
    bool isRunning() const { return running_.load(); }

    //
    // Status
    //

    // Get statistics
    Statistics getStatistics() const;

    // Reset statistics
    void resetStatistics();

    // Get connection status
    bool isConnected() const;

    // Get time since last packet (milliseconds)
    int64_t getTimeSinceLastPacket() const;

    //
    // Configuration
    //

    // Update channel mapping (stops and restarts receiver)
    bool updateMapping(const ChannelMapping& newMapping);

    // Get current SDP session
    const SDPSession& getSDPSession() const { return sdp_; }

    // Get current mapping
    const ChannelMapping& getMapping() const { return mapping_; }

private:
    // Network thread function
    void receiveLoop();

    // Packet processing
    void processPacket(mblk_t* packet);
    bool validatePacket(mblk_t* packet);

    // Audio decoding
    void decodeL16(const uint8_t* payload, size_t payloadSize);
    void decodeL24(const uint8_t* payload, size_t payloadSize);

    // Channel mapping: stream audio → device channels
    void mapChannelsToDevice(const float* interleavedAudio, size_t frameCount);

    // Statistics tracking
    void updateStats(uint16_t sequenceNumber, size_t payloadSize);

    // Configuration
    SDPSession sdp_;
    ChannelMapping mapping_;
    DeviceChannelBuffers& deviceChannels_;

    // oRTP session
    RtpSession* rtpSession_{nullptr};

    // Threading
    std::thread receiveThread_;
    std::atomic<bool> running_{false};

    // Statistics
    Statistics stats_;
    mutable std::mutex statsMutex_;
    uint16_t lastSequenceNumber_{0};
    uint32_t lastTimestamp_{0};

    // Connection state
    std::atomic<bool> connected_{false};
    std::chrono::steady_clock::time_point lastPacketTime_;

    // Audio buffer (reused to avoid allocations)
    std::vector<float> audioBuffer_;
};

} // namespace AES67
