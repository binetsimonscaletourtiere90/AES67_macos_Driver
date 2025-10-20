//
// AES67Device.h
// AES67 macOS Driver - Build #1
// Core Audio device implementation using libASPL
// 128-channel input/output AudioServerPlugIn
//

#pragma once

#include "../Shared/Types.h"
#include "../Shared/RingBuffer.hpp"
#include <aspl/Device.hpp>
#include <aspl/Stream.hpp>
#include <aspl/Context.hpp>
#include <memory>
#include <array>
#include <atomic>

namespace AES67 {

class AES67IOHandler;
class StreamManager;

//
// AES67 Audio Device
//
// 128-channel bidirectional Core Audio device
// Integrates with macOS Core Audio via AudioServerPlugIn (libASPL)
//
class AES67Device : public aspl::Device {
public:
    static constexpr size_t kNumChannels = 128;

    // Supported sample rates
    static constexpr std::array<Float64, 8> kSupportedSampleRates = {
        44100.0, 48000.0, 88200.0, 96000.0,
        176400.0, 192000.0, 352800.0, 384000.0
    };

    // Supported buffer sizes (in samples)
    static constexpr std::array<UInt32, 8> kSupportedBufferSizes = {
        16, 32, 48, 64, 128, 192, 288, 480
    };

    //
    // Constructor
    //
    explicit AES67Device(std::shared_ptr<aspl::Context> context);
    ~AES67Device();

    // Initialize device (must be called after construction)
    void Initialize();

    //
    // Device Configuration
    //

    // Get/Set sample rate
    Float64 GetSampleRate() const;
    OSStatus SetSampleRate(Float64 sampleRate);

    // Get available sample rates
    std::vector<AudioValueRange> GetAvailableSampleRates() const override;

    // Get/Set buffer size
    UInt32 GetBufferSize() const;
    OSStatus SetBufferSize(UInt32 bufferSize);

    // Get available buffer sizes
    std::vector<UInt32> GetAvailableBufferSizes() const;

    //
    // Device Information
    //

    std::string GetDeviceName() const;
    std::string GetDeviceManufacturer() const;
    std::string GetDeviceUID() const override;

    // Get channel count
    UInt32 GetInputChannelCount() const { return kNumChannels; }
    UInt32 GetOutputChannelCount() const { return kNumChannels; }

    //
    // Stream Access
    //

    // Get input/output streams
    std::shared_ptr<aspl::Stream> GetInputStream() const { return inputStream_; }
    std::shared_ptr<aspl::Stream> GetOutputStream() const { return outputStream_; }

    //
    // Ring Buffer Access (for NetworkEngine)
    //

    using DeviceChannelBuffers = std::array<SPSCRingBuffer<float>, kNumChannels>;

    DeviceChannelBuffers& GetInputBuffers() { return inputBuffers_; }
    DeviceChannelBuffers& GetOutputBuffers() { return outputBuffers_; }

    //
    // Control
    //

    // Start/Stop IO
    OSStatus StartIO();
    OSStatus StopIO();
    bool IsIORunning() const { return ioRunning_.load(); }

    //
    // Statistics
    //

    uint64_t GetInputUnderrunCount() const { return inputUnderruns_.load(); }
    uint64_t GetOutputUnderrunCount() const { return outputUnderruns_.load(); }
    void ResetStatistics();

private:
    // Internal handlers
    OSStatus OnSetSampleRate(Float64 sampleRate);
    OSStatus OnSetBufferSize(UInt32 bufferSize);

    // Initialize streams and IO handler
    void InitializeStreams();
    void InitializeIOHandler();

    // Calculate optimal ring buffer size based on sample rate
    // Returns size for desired latency (default: 2ms safety margin)
    static size_t CalculateRingBufferSize(Float64 sampleRate, double latencyMs = 2.0);

    // Ring buffers for audio data
    // Network threads write to input buffers, read from output buffers
    // Core Audio thread reads from input buffers, writes to output buffers
    DeviceChannelBuffers inputBuffers_;   // Network → CoreAudio
    DeviceChannelBuffers outputBuffers_;  // CoreAudio → Network

    // Streams
    std::shared_ptr<aspl::Stream> inputStream_;
    std::shared_ptr<aspl::Stream> outputStream_;

    // IO Handler
    std::shared_ptr<AES67IOHandler> ioHandler_;

    // Current configuration
    std::atomic<Float64> currentSampleRate_{48000.0};
    std::atomic<UInt32> currentBufferSize_{64};

    // State
    std::atomic<bool> ioRunning_{false};

    // Statistics
    std::atomic<uint64_t> inputUnderruns_{0};
    std::atomic<uint64_t> outputUnderruns_{0};

    // Constants
    static constexpr size_t kDefaultRingBufferSize = 480;  // Samples (1ms @ 384kHz)
};

} // namespace AES67
