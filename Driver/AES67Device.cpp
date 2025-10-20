//
// AES67Device.cpp
// AES67 macOS Driver - Build #7
// Core Audio device implementation
//

#include "AES67Device.h"
#include "AES67IOHandler.h"
#include "DebugLog.h"
#include <CoreAudio/AudioServerPlugIn.h>
#include <utility>

namespace AES67 {

// Helper to create initialized ring buffer array
namespace {
    template<size_t... Is>
    auto MakeRingBufferArray(size_t bufferSize, std::index_sequence<Is...>) {
        return std::array<SPSCRingBuffer<float>, sizeof...(Is)>{
            ((void)Is, SPSCRingBuffer<float>(bufferSize))...
        };
    }

    auto MakeRingBufferArray(size_t bufferSize) {
        return MakeRingBufferArray(bufferSize, std::make_index_sequence<AES67Device::kNumChannels>{});
    }
}

AES67Device::AES67Device(std::shared_ptr<aspl::Context> context)
    : aspl::Device(context, aspl::DeviceParameters{
        .Name = "AES67 Device",
        .Manufacturer = "AES67 Driver",
        .DeviceUID = "com.aes67.driver.device",
        .ModelUID = "com.aes67.driver.model",
        .CanBeDefault = true,
        .CanBeDefaultForSystemSounds = false
    })
    // Calculate optimal ring buffer size based on initial sample rate
    , inputBuffers_(MakeRingBufferArray(
          CalculateRingBufferSize(currentSampleRate_.load())))
    , outputBuffers_(MakeRingBufferArray(
          CalculateRingBufferSize(currentSampleRate_.load())))
{
    AES67_LOG("AES67Device constructor: Starting initialization");
    AES67_LOGF("AES67Device: Sample rate = %.0f Hz", currentSampleRate_.load());
    AES67_LOGF("AES67Device: Ring buffer size = %zu samples",
               CalculateRingBufferSize(currentSampleRate_.load()));

    // NOTE: Cannot call InitializeStreams() here because shared_from_this()
    // won't work until the shared_ptr is fully constructed
    // InitializeStreams() will be called from Initialize() method

    AES67_LOG("AES67Device constructor: Basic initialization complete");
}

void AES67Device::Initialize() {
    AES67_LOG("AES67Device::Initialize() called");

    // Initialize streams
    AES67_LOG("AES67Device: Calling InitializeStreams()");
    InitializeStreams();

    // Initialize IO handler
    AES67_LOG("AES67Device: Calling InitializeIOHandler()");
    InitializeIOHandler();

    AES67_LOG("AES67Device::Initialize() complete");
}

AES67Device::~AES67Device() {
    StopIO();
}

void AES67Device::InitializeStreams() {
    AES67_LOG("InitializeStreams: Creating input stream (Network → Core Audio)");
    // Create input stream (Network → Core Audio)
    aspl::StreamParameters inputParams;
    inputParams.Direction = aspl::Direction::Input;
    inputParams.StartingChannel = 1;
    inputParams.Format.mSampleRate = currentSampleRate_.load();
    inputParams.Format.mFormatID = kAudioFormatLinearPCM;
    inputParams.Format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    inputParams.Format.mBitsPerChannel = 32;
    inputParams.Format.mChannelsPerFrame = kNumChannels;
    inputParams.Format.mBytesPerFrame = kNumChannels * sizeof(float);
    inputParams.Format.mFramesPerPacket = 1;
    inputParams.Format.mBytesPerPacket = inputParams.Format.mBytesPerFrame;

    AES67_LOGF("InitializeStreams: Input stream - %u channels @ %.0f Hz",
               kNumChannels, currentSampleRate_.load());

    inputStream_ = std::make_shared<aspl::Stream>(
        GetContext(),
        std::static_pointer_cast<aspl::Device>(shared_from_this()),
        inputParams
    );
    AES67_LOG("InitializeStreams: Input stream created, adding to device");
    AddStreamAsync(inputStream_);
    AES67_LOG("InitializeStreams: Input stream added successfully");

    // Create output stream (Core Audio → Network)
    AES67_LOG("InitializeStreams: Creating output stream (Core Audio → Network)");
    aspl::StreamParameters outputParams;
    outputParams.Direction = aspl::Direction::Output;
    outputParams.StartingChannel = 1;
    outputParams.Format.mSampleRate = currentSampleRate_.load();
    outputParams.Format.mFormatID = kAudioFormatLinearPCM;
    outputParams.Format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    outputParams.Format.mBitsPerChannel = 32;
    outputParams.Format.mChannelsPerFrame = kNumChannels;
    outputParams.Format.mBytesPerFrame = kNumChannels * sizeof(float);
    outputParams.Format.mFramesPerPacket = 1;
    outputParams.Format.mBytesPerPacket = outputParams.Format.mBytesPerFrame;

    AES67_LOGF("InitializeStreams: Output stream - %u channels @ %.0f Hz",
               kNumChannels, currentSampleRate_.load());

    outputStream_ = std::make_shared<aspl::Stream>(
        GetContext(),
        std::static_pointer_cast<aspl::Device>(shared_from_this()),
        outputParams
    );
    AES67_LOG("InitializeStreams: Output stream created, adding to device");
    AddStreamAsync(outputStream_);
    AES67_LOG("InitializeStreams: Output stream added successfully");

    AES67_LOG("InitializeStreams: Complete");
}

void AES67Device::InitializeIOHandler() {
    AES67_LOG("InitializeIOHandler: Creating AES67IOHandler");
    ioHandler_ = std::make_shared<AES67IOHandler>(
        inputBuffers_,
        outputBuffers_,
        inputUnderruns_,
        outputUnderruns_
    );
    AES67_LOG("InitializeIOHandler: IOHandler created successfully");

    // Register IO handler with device
    AES67_LOG("InitializeIOHandler: Registering IOHandler with device");
    SetIOHandler(ioHandler_);
    AES67_LOG("InitializeIOHandler: Complete");
}

Float64 AES67Device::GetSampleRate() const {
    return currentSampleRate_.load();
}

OSStatus AES67Device::SetSampleRate(Float64 sampleRate) {
    // Validate sample rate
    bool isValid = false;
    for (auto validRate : kSupportedSampleRates) {
        if (std::abs(sampleRate - validRate) < 0.1) {
            isValid = true;
            break;
        }
    }

    if (!isValid) {
        return kAudioHardwareUnsupportedOperationError;
    }

    // Update current sample rate
    currentSampleRate_.store(sampleRate);

    // Update stream formats
    if (inputStream_) {
        auto format = inputStream_->GetPhysicalFormat();
        format.mSampleRate = sampleRate;
        inputStream_->SetPhysicalFormatAsync(format);
    }
    if (outputStream_) {
        auto format = outputStream_->GetPhysicalFormat();
        format.mSampleRate = sampleRate;
        outputStream_->SetPhysicalFormatAsync(format);
    }

    return kAudioHardwareNoError;
}

std::vector<AudioValueRange> AES67Device::GetAvailableSampleRates() const {
    std::vector<AudioValueRange> ranges;
    for (auto rate : kSupportedSampleRates) {
        ranges.push_back({rate, rate});
    }
    return ranges;
}

UInt32 AES67Device::GetBufferSize() const {
    return currentBufferSize_.load();
}

OSStatus AES67Device::SetBufferSize(UInt32 bufferSize) {
    // Validate buffer size
    bool isValid = false;
    for (auto validSize : kSupportedBufferSizes) {
        if (bufferSize == validSize) {
            isValid = true;
            break;
        }
    }

    if (!isValid) {
        return kAudioHardwareUnsupportedOperationError;
    }

    // Update current buffer size
    currentBufferSize_.store(bufferSize);

    return kAudioHardwareNoError;
}

std::vector<UInt32> AES67Device::GetAvailableBufferSizes() const {
    return std::vector<UInt32>(kSupportedBufferSizes.begin(), kSupportedBufferSizes.end());
}

std::string AES67Device::GetDeviceName() const {
    return "AES67 Device";
}

std::string AES67Device::GetDeviceManufacturer() const {
    return "AES67 Driver";
}

std::string AES67Device::GetDeviceUID() const {
    return "com.aes67.driver.device";
}

OSStatus AES67Device::StartIO() {
    if (ioRunning_.load()) {
        return kAudioHardwareNoError; // Already running
    }

    // Start streams
    if (inputStream_) {
        inputStream_->SetIsActive(true);
    }
    if (outputStream_) {
        outputStream_->SetIsActive(true);
    }

    ioRunning_.store(true);

    return kAudioHardwareNoError;
}

OSStatus AES67Device::StopIO() {
    if (!ioRunning_.load()) {
        return kAudioHardwareNoError; // Already stopped
    }

    // Stop streams
    if (inputStream_) {
        inputStream_->SetIsActive(false);
    }
    if (outputStream_) {
        outputStream_->SetIsActive(false);
    }

    ioRunning_.store(false);

    return kAudioHardwareNoError;
}

void AES67Device::ResetStatistics() {
    inputUnderruns_.store(0);
    outputUnderruns_.store(0);
}

OSStatus AES67Device::OnSetSampleRate(Float64 sampleRate) {
    return SetSampleRate(sampleRate);
}

OSStatus AES67Device::OnSetBufferSize(UInt32 bufferSize) {
    return SetBufferSize(bufferSize);
}

size_t AES67Device::CalculateRingBufferSize(Float64 sampleRate, double latencyMs) {
    // Calculate ring buffer size for desired latency
    // Formula: samples = (sampleRate × latencyMs) / 1000
    //
    // Examples:
    //   48kHz @ 2ms = 96 samples
    //   96kHz @ 2ms = 192 samples
    //   384kHz @ 2ms = 768 samples
    //
    // This provides a safety buffer for network jitter and processing delays

    const size_t calculatedSize = static_cast<size_t>(
        (sampleRate * latencyMs) / 1000.0
    );

    // Ensure minimum size (at least 64 samples)
    constexpr size_t kMinRingBufferSize = 64;

    // Ensure maximum size (prevent excessive memory use)
    constexpr size_t kMaxRingBufferSize = 2048;

    // Clamp to valid range
    if (calculatedSize < kMinRingBufferSize) {
        return kMinRingBufferSize;
    }
    if (calculatedSize > kMaxRingBufferSize) {
        return kMaxRingBufferSize;
    }

    return calculatedSize;
}

} // namespace AES67
