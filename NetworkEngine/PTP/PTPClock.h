//
// PTPClock.h
// AES67 macOS Driver - Build #1
// PTP (IEEE 1588-2008) clock implementation with multi-domain support
//

#pragma once

#include "../../Shared/Types.h"
#include <thread>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>

namespace AES67 {

// Forward declarations
struct SDPSession;

//
// Local Clock (fallback when PTP not available)
//
class LocalClock {
public:
    LocalClock();

    // Get current time in nanoseconds
    uint64_t getTime() const;

    // Get time in microseconds
    uint64_t getTimeMicroseconds() const;
};

//
// PTP Clock
//
// Single PTP domain clock instance
// Runs ptpd daemon and synchronizes to network master
//
class PTPClock {
public:
    explicit PTPClock(int domain);
    ~PTPClock();

    // Prevent copy/move
    PTPClock(const PTPClock&) = delete;
    PTPClock& operator=(const PTPClock&) = delete;

    //
    // Control
    //

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    //
    // Time Access
    //

    // Get current PTP time (nanoseconds)
    uint64_t getTime() const;

    // Get PTP time in microseconds
    uint64_t getTimeMicroseconds() const;

    //
    // Status
    //

    bool isLocked() const { return locked_.load(); }
    int64_t getOffsetNs() const { return offsetNs_.load(); }
    int getDomain() const { return domain_; }

    // Get master clock ID
    std::string getMasterClockID() const;

    // Get clock quality
    uint8_t getClockClass() const;
    uint8_t getClockAccuracy() const;

private:
    // PTP management thread
    void ptpThread();

    // Update lock status
    void updateLockStatus();

    int domain_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> locked_{false};
    std::atomic<int64_t> offsetNs_{0};

    // PTP state
    mutable std::mutex stateMutex_;
    std::string masterClockID_;
    uint8_t clockClass_{248};     // Default: not locked
    uint8_t clockAccuracy_{254};   // Unknown

    // Local clock for time calculation
    std::unique_ptr<LocalClock> localClock_;
};

//
// PTP Clock Manager
//
// Manages multiple PTP clocks (one per domain)
// Provides unified interface for time access
//
class PTPClockManager {
public:
    static PTPClockManager& getInstance();

    //
    // Control
    //

    // Enable/disable PTP globally
    void setPTPEnabled(bool enabled);
    bool isPTPEnabled() const { return globalEnabled_.load(); }

    //
    // Clock Management
    //

    // Get or create PTP clock for domain
    std::shared_ptr<PTPClock> getClockForDomain(int domain);

    // Remove clock for domain
    void removeClock(int domain);

    // Get all active domains
    std::vector<int> getActiveDomains() const;

    //
    // Time Access
    //

    // Get time for specific stream (uses stream's PTP domain or fallback)
    uint64_t getTimeForStream(const SDPSession& sdp);

    // Get time for specific domain (or fallback if not available)
    uint64_t getTimeForDomain(int domain);

    // Get fallback local time
    uint64_t getLocalTime() const;

private:
    PTPClockManager();
    ~PTPClockManager() = default;

    // Prevent copy/move
    PTPClockManager(const PTPClockManager&) = delete;
    PTPClockManager& operator=(const PTPClockManager&) = delete;

    // Clock instances
    std::map<int, std::shared_ptr<PTPClock>> clocks_;
    mutable std::mutex clocksMutex_;

    // Global enable/disable
    std::atomic<bool> globalEnabled_{true};

    // Fallback clock
    std::unique_ptr<LocalClock> fallbackClock_;
};

} // namespace AES67
