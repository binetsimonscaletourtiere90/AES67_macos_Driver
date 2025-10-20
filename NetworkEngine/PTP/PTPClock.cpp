//
// PTPClock.cpp
// AES67 macOS Driver - Build #6
// Multi-domain PTP clock implementation with graceful fallback
//

#include "PTPClock.h"
#include "../../Driver/SDPParser.h"
#include <chrono>
#include <cstring>

namespace AES67 {

//
// LocalClock Implementation
//

LocalClock::LocalClock() {
}

uint64_t LocalClock::getTime() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    return nanos.count();
}

uint64_t LocalClock::getTimeMicroseconds() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    return micros.count();
}

//
// PTPClock Implementation
//

PTPClock::PTPClock(int domain)
    : domain_(domain)
    , running_(false)
    , locked_(false)
    , offsetNs_(0)
    , localClock_(std::make_unique<LocalClock>())
{
}

PTPClock::~PTPClock() {
    stop();
}

bool PTPClock::start() {
    if (running_) {
        return false;
    }

    running_ = true;
    thread_ = std::thread(&PTPClock::ptpThread, this);

    return true;
}

void PTPClock::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (thread_.joinable()) {
        thread_.join();
    }
}

uint64_t PTPClock::getTime() const {
    // Get local time
    uint64_t localTime = localClock_->getTime();

    // Apply PTP offset if locked
    if (locked_.load()) {
        int64_t offset = offsetNs_.load();
        return localTime + offset;
    }

    return localTime;
}

uint64_t PTPClock::getTimeMicroseconds() const {
    return getTime() / 1000;
}

std::string PTPClock::getMasterClockID() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return masterClockID_;
}

uint8_t PTPClock::getClockClass() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return clockClass_;
}

uint8_t PTPClock::getClockAccuracy() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return clockAccuracy_;
}

void PTPClock::ptpThread() {
    // Simulate PTP synchronization
    // In a real implementation, this would use ptpd library

    while (running_) {
        // Simulate lock acquisition after 2 seconds
        static int iterations = 0;
        iterations++;

        if (iterations > 20) { // ~2 seconds with 100ms sleep
            updateLockStatus();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PTPClock::updateLockStatus() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    locked_ = true;
    // Simulate small offset (in real implementation, this comes from PTP protocol)
    offsetNs_ = 100; // 100ns offset
    clockClass_ = 6;  // Locked to primary reference
    clockAccuracy_ = 0x20;  // Within 25 ns
    masterClockID_ = "00:00:00:00:00:00:00:00";  // Placeholder
}

//
// PTPClockManager Implementation
//

PTPClockManager::PTPClockManager()
    : fallbackClock_(std::make_unique<LocalClock>())
{
}

PTPClockManager& PTPClockManager::getInstance() {
    static PTPClockManager instance;
    return instance;
}

void PTPClockManager::setPTPEnabled(bool enabled) {
    globalEnabled_ = enabled;
}

std::shared_ptr<PTPClock> PTPClockManager::getClockForDomain(int domain) {
    std::lock_guard<std::mutex> lock(clocksMutex_);

    auto it = clocks_.find(domain);
    if (it != clocks_.end()) {
        return it->second;
    }

    // Create new clock for this domain
    auto clock = std::make_shared<PTPClock>(domain);
    clock->start();
    clocks_[domain] = clock;

    return clock;
}

void PTPClockManager::removeClock(int domain) {
    std::lock_guard<std::mutex> lock(clocksMutex_);

    auto it = clocks_.find(domain);
    if (it != clocks_.end()) {
        it->second->stop();
        clocks_.erase(it);
    }
}

std::vector<int> PTPClockManager::getActiveDomains() const {
    std::lock_guard<std::mutex> lock(clocksMutex_);

    std::vector<int> domains;
    for (const auto& pair : clocks_) {
        domains.push_back(pair.first);
    }

    return domains;
}

uint64_t PTPClockManager::getTimeForStream(const SDPSession& sdp) {
    if (!globalEnabled_) {
        return fallbackClock_->getTime();
    }

    // Extract PTP domain from SDP session
    int domain = sdp.ptpDomain;

    return getTimeForDomain(domain);
}

uint64_t PTPClockManager::getTimeForDomain(int domain) {
    if (!globalEnabled_) {
        return fallbackClock_->getTime();
    }

    std::lock_guard<std::mutex> lock(clocksMutex_);

    auto it = clocks_.find(domain);
    if (it != clocks_.end()) {
        // Return PTP clock time if locked
        if (it->second->isLocked()) {
            return it->second->getTime();
        }
    }

    // Fallback to local clock if PTP not available or not locked
    return fallbackClock_->getTime();
}

uint64_t PTPClockManager::getLocalTime() const {
    return fallbackClock_->getTime();
}

} // namespace AES67
