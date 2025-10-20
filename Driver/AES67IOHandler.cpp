//
// AES67IOHandler.cpp
// AES67 macOS Driver - Build #6
// Real-time safe audio I/O handler
// RT-SAFE: NO ALLOCATION, NO LOCKS, NO BLOCKING
//

#include "AES67IOHandler.h"
#include <cstring>
#include <algorithm>

namespace AES67 {

AES67IOHandler::AES67IOHandler(
    DeviceChannelBuffers& inputBuffers,
    DeviceChannelBuffers& outputBuffers,
    std::atomic<uint64_t>& inputUnderruns,
    std::atomic<uint64_t>& outputUnderruns
)
    : inputBuffers_(inputBuffers)
    , outputBuffers_(outputBuffers)
    , inputUnderruns_(inputUnderruns)
    , outputUnderruns_(outputUnderruns)
{
}

AES67IOHandler::~AES67IOHandler() {
}

OSStatus AES67IOHandler::OnReadClientInput(
    const std::shared_ptr<aspl::Stream>& stream,
    Float64 timestamp,
    const void* inputData,
    void* outputData,
    UInt32 frameCount
) {
    // RT-SAFE: Read from ring buffers (Network → Core Audio)
    // This provides INPUT audio to the client (DAW)

    if (!outputData || !stream) {
        return kAudioHardwareUnspecifiedError;
    }

    // Validate channel count matches our buffer configuration
    const UInt32 channelCount = stream->GetPhysicalFormat().mChannelsPerFrame;
    if (channelCount != kNumChannels) {
        // Channel count mismatch - this is a configuration error
        // Fill with silence to prevent crashes
        std::memset(outputData, 0, frameCount * channelCount * sizeof(float));
        return kAudioHardwareUnspecifiedError;
    }

    float* output = static_cast<float*>(outputData);

    // Process input (read from ring buffers) - batch processing for performance
    processInput(output, frameCount, channelCount);

    // Note: timestamp parameter reserved for future PTP synchronization
    (void)timestamp;  // Suppress unused parameter warning

    return kAudioHardwareNoError;
}

OSStatus AES67IOHandler::OnWriteClientOutput(
    const std::shared_ptr<aspl::Stream>& stream,
    Float64 timestamp,
    const void* inputData,
    void* outputData,
    UInt32 frameCount
) {
    // RT-SAFE: Write to ring buffers (Core Audio → Network)
    // This receives OUTPUT audio from the client (DAW)

    if (!inputData || !stream) {
        return kAudioHardwareUnspecifiedError;
    }

    // Validate channel count matches our buffer configuration
    const UInt32 channelCount = stream->GetPhysicalFormat().mChannelsPerFrame;
    if (channelCount != kNumChannels) {
        // Channel count mismatch - discard data to prevent crashes
        return kAudioHardwareUnspecifiedError;
    }

    const float* input = static_cast<const float*>(inputData);

    // Process output (write to ring buffers) - batch processing for performance
    processOutput(input, frameCount, channelCount);

    // Note: timestamp parameter reserved for future PTP synchronization
    (void)timestamp;  // Suppress unused parameter warning

    return kAudioHardwareNoError;
}

void AES67IOHandler::processInput(float* outputData, UInt32 frameCount, UInt32 channelCount) noexcept {
    // RT-SAFE: Read from input ring buffers (Network → Core Audio)
    // Network threads write to inputBuffers_
    // Core Audio reads from inputBuffers_ here
    //
    // PERFORMANCE OPTIMIZED: Batch reads per channel instead of per-sample
    // This reduces ring buffer calls from (frameCount × channelCount) to (channelCount)
    // Example: 64 frames × 128 channels = 8,192 → 128 calls (64× reduction!)

    // Stack-allocated temporary buffer (RT-safe, no heap allocation)
    // Maximum supported buffer size to avoid VLAs
    constexpr UInt32 kMaxFramesPerBuffer = 512;
    float channelBuffer[kMaxFramesPerBuffer];

    // Safety check - should never happen in production
    if (frameCount > kMaxFramesPerBuffer) {
        // Fall back to silence if buffer is too large
        std::memset(outputData, 0, frameCount * channelCount * sizeof(float));
        return;
    }

    bool hadUnderrun = false;

    // Process each channel independently
    for (size_t ch = 0; ch < channelCount; ++ch) {
        // BATCH READ: Read all frames for this channel at once
        const size_t samplesRead = inputBuffers_[ch].read(channelBuffer, frameCount);

        // Check for underrun
        if (samplesRead < frameCount) {
            // Underrun detected: fill remainder with silence
            std::memset(&channelBuffer[samplesRead], 0,
                       (frameCount - samplesRead) * sizeof(float));

            // Count underrun only once per callback (not per channel)
            if (!hadUnderrun) {
                inputUnderruns_.fetch_add(1, std::memory_order_relaxed);
                hadUnderrun = true;
            }
        }

        // De-interleave from channel buffer into interleaved output
        // outputData layout: [ch0_f0, ch1_f0, ..., ch127_f0, ch0_f1, ch1_f1, ...]
        for (UInt32 frame = 0; frame < frameCount; ++frame) {
            outputData[frame * channelCount + ch] = channelBuffer[frame];
        }
    }
}

void AES67IOHandler::processOutput(const float* inputData, UInt32 frameCount, UInt32 channelCount) noexcept {
    // RT-SAFE: Write to output ring buffers (Core Audio → Network)
    // Core Audio writes to outputBuffers_ here
    // Network threads read from outputBuffers_
    //
    // PERFORMANCE OPTIMIZED: Batch writes per channel instead of per-sample
    // This reduces ring buffer calls from (frameCount × channelCount) to (channelCount)
    // Example: 64 frames × 128 channels = 8,192 → 128 calls (64× reduction!)

    // Stack-allocated temporary buffer (RT-safe, no heap allocation)
    constexpr UInt32 kMaxFramesPerBuffer = 512;
    float channelBuffer[kMaxFramesPerBuffer];

    // Safety check - should never happen in production
    if (frameCount > kMaxFramesPerBuffer) {
        // Silently discard data if buffer is too large
        return;
    }

    bool hadOverrun = false;

    // Process each channel independently
    for (size_t ch = 0; ch < channelCount; ++ch) {
        // Interleave from input into channel buffer
        // inputData layout: [ch0_f0, ch1_f0, ..., ch127_f0, ch0_f1, ch1_f1, ...]
        for (UInt32 frame = 0; frame < frameCount; ++frame) {
            channelBuffer[frame] = inputData[frame * channelCount + ch];
        }

        // BATCH WRITE: Write all frames for this channel at once
        const size_t samplesWritten = outputBuffers_[ch].write(channelBuffer, frameCount);

        // Check for overrun
        if (samplesWritten < frameCount) {
            // Overrun detected: buffer was full, some samples were dropped
            // Count overrun only once per callback (not per channel)
            if (!hadOverrun) {
                outputUnderruns_.fetch_add(1, std::memory_order_relaxed);
                hadOverrun = true;
            }
            // Note: Dropped samples = (frameCount - samplesWritten)
            // This is acceptable for RT audio - cannot block waiting for space
        }
    }
}

} // namespace AES67
