//
// SAPListener.h
// AES67 macOS Driver - Build #1
// SAP (Session Announcement Protocol) listener for AES67 stream discovery
//

#pragma once

#include "../../Shared/Types.h"
#include "../../Driver/SDPParser.h"
#include <thread>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

namespace AES67 {

//
// SAP Announcement
//
struct SAPAnnouncement {
    uint32_t messageHash;       // Hash of announcement for deduplication
    std::string origin;         // Originating IP address
    SDPSession sdp;             // SDP session description
    std::chrono::steady_clock::time_point lastSeen;
    bool isDelete{false};       // true if this is a deletion announcement
};

//
// SAP Listener
//
// Listens for SAP announcements on 239.255.255.255:9875 (default AES67)
// Automatically discovers AES67 streams on the network
//
class SAPListener {
public:
    using DiscoveryCallback = std::function<void(const SDPSession&)>;
    using DeletionCallback = std::function<void(uint32_t messageHash)>;

    SAPListener();
    ~SAPListener();

    // Prevent copy/move
    SAPListener(const SAPListener&) = delete;
    SAPListener& operator=(const SAPListener&) = delete;

    //
    // Control
    //

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    //
    // Configuration
    //

    // Set multicast address and port (default: 239.255.255.255:9875)
    void setMulticastAddress(const std::string& address, uint16_t port);

    //
    // Callbacks
    //

    // Called when a new stream is discovered or updated
    void setDiscoveryCallback(DiscoveryCallback callback) {
        discoveryCallback_ = callback;
    }

    // Called when a stream announcement is deleted
    void setDeletionCallback(DeletionCallback callback) {
        deletionCallback_ = callback;
    }

    //
    // Query
    //

    // Get all discovered streams
    std::vector<SDPSession> getDiscoveredStreams() const;

    // Get announcement count
    size_t getAnnouncementCount() const;

    // Clear all discovered streams
    void clearDiscoveredStreams();

private:
    // Listen thread function
    void listenLoop();

    // Process received SAP packet
    void processSAPPacket(const uint8_t* data, size_t length, const std::string& sourceIP);

    // Parse SAP header
    bool parseSAPHeader(const uint8_t* data, size_t length,
                       bool& isDelete, uint32_t& messageHash, size_t& sdpOffset);

    // Update announcement cache
    void updateAnnouncement(const SAPAnnouncement& announcement);

    // Remove old announcements (timeout)
    void cleanupOldAnnouncements();

    // Network configuration
    std::string multicastAddress_{"239.255.255.255"};
    uint16_t port_{9875};
    int socket_{-1};

    // Threading
    std::thread listenThread_;
    std::atomic<bool> running_{false};

    // Announcement cache
    std::map<uint32_t, SAPAnnouncement> announcements_;
    mutable std::mutex announcementsMutex_;

    // Callbacks
    DiscoveryCallback discoveryCallback_;
    DeletionCallback deletionCallback_;

    // Cleanup interval
    std::chrono::seconds cleanupInterval_{60};
    std::chrono::steady_clock::time_point lastCleanup_;
};

} // namespace AES67
