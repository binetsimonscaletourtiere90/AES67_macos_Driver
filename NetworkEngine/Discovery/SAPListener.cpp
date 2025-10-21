//
// SAPListener.cpp
// AES67 macOS Driver - Build #16
// SAP (Session Announcement Protocol) listener implementation
// RFC 2974 - Session Announcement Protocol
//

#include "SAPListener.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace AES67 {

//
// Constructor
//
SAPListener::SAPListener()
    : lastCleanup_(std::chrono::steady_clock::now())
{
}

//
// Destructor
//
SAPListener::~SAPListener() {
    stop();
}

//
// Start listening for SAP announcements
//
bool SAPListener::start() {
    if (running_.load()) {
        return true;  // Already running
    }

    // Create UDP socket
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        std::cerr << "SAPListener: Failed to create socket" << std::endl;
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "SAPListener: Failed to set SO_REUSEADDR" << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }

#ifdef SO_REUSEPORT
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "SAPListener: Warning - Failed to set SO_REUSEPORT" << std::endl;
    }
#endif

    // Bind to port
    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = INADDR_ANY;
    bindAddr.sin_port = htons(port_);

    if (bind(socket_, (sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        std::cerr << "SAPListener: Failed to bind to port " << port_ << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }

    // Join multicast group
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(multicastAddress_.c_str());
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::cerr << "SAPListener: Failed to join multicast group " << multicastAddress_ << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }

    // Start listen thread
    running_.store(true);
    listenThread_ = std::thread(&SAPListener::listenLoop, this);

    std::cout << "SAPListener: Started on " << multicastAddress_ << ":" << port_ << std::endl;
    return true;
}

//
// Stop listening
//
void SAPListener::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    // Close socket to unblock recv
    if (socket_ >= 0) {
        shutdown(socket_, SHUT_RDWR);
        close(socket_);
        socket_ = -1;
    }

    // Wait for thread to finish
    if (listenThread_.joinable()) {
        listenThread_.join();
    }

    std::cout << "SAPListener: Stopped" << std::endl;
}

//
// Set multicast address and port
//
void SAPListener::setMulticastAddress(const std::string& address, uint16_t port) {
    if (running_.load()) {
        std::cerr << "SAPListener: Cannot change address while running" << std::endl;
        return;
    }
    multicastAddress_ = address;
    port_ = port;
}

//
// Get all discovered streams
//
std::vector<SDPSession> SAPListener::getDiscoveredStreams() const {
    std::lock_guard<std::mutex> lock(announcementsMutex_);
    std::vector<SDPSession> streams;
    streams.reserve(announcements_.size());

    for (const auto& [hash, announcement] : announcements_) {
        if (!announcement.isDelete) {
            streams.push_back(announcement.sdp);
        }
    }

    return streams;
}

//
// Get announcement count
//
size_t SAPListener::getAnnouncementCount() const {
    std::lock_guard<std::mutex> lock(announcementsMutex_);
    return announcements_.size();
}

//
// Clear all discovered streams
//
void SAPListener::clearDiscoveredStreams() {
    std::lock_guard<std::mutex> lock(announcementsMutex_);
    announcements_.clear();
}

//
// Listen loop (runs in separate thread)
//
void SAPListener::listenLoop() {
    uint8_t buffer[65536];

    while (running_.load()) {
        // Receive packet
        sockaddr_in senderAddr{};
        socklen_t senderAddrLen = sizeof(senderAddr);

        ssize_t bytesReceived = recvfrom(socket_, buffer, sizeof(buffer), 0,
                                        (sockaddr*)&senderAddr, &senderAddrLen);

        if (bytesReceived < 0) {
            if (running_.load()) {
                std::cerr << "SAPListener: recvfrom error" << std::endl;
            }
            break;
        }

        if (bytesReceived > 0) {
            // Get source IP
            char sourceIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &senderAddr.sin_addr, sourceIP, sizeof(sourceIP));

            // Process packet
            processSAPPacket(buffer, bytesReceived, sourceIP);
        }

        // Periodic cleanup
        auto now = std::chrono::steady_clock::now();
        if (now - lastCleanup_ > cleanupInterval_) {
            cleanupOldAnnouncements();
            lastCleanup_ = now;
        }
    }
}

//
// Process received SAP packet
//
void SAPListener::processSAPPacket(const uint8_t* data, size_t length, const std::string& sourceIP) {
    if (length < 4) {
        return;  // Too short
    }

    // Parse SAP header
    bool isDelete = false;
    uint32_t messageHash = 0;
    size_t sdpOffset = 0;

    if (!parseSAPHeader(data, length, isDelete, messageHash, sdpOffset)) {
        return;
    }

    if (sdpOffset >= length) {
        return;  // No SDP payload
    }

    // Extract SDP text
    std::string sdpText(reinterpret_cast<const char*>(data + sdpOffset),
                       length - sdpOffset);

    // Handle deletion
    if (isDelete) {
        std::lock_guard<std::mutex> lock(announcementsMutex_);
        auto it = announcements_.find(messageHash);
        if (it != announcements_.end()) {
            announcements_.erase(it);
            if (deletionCallback_) {
                deletionCallback_(messageHash);
            }
        }
        return;
    }

    // Parse SDP
    auto sdpSession = SDPParser::parseString(sdpText);
    if (!sdpSession) {
        return;  // Invalid SDP
    }

    // Create announcement
    SAPAnnouncement announcement;
    announcement.messageHash = messageHash;
    announcement.origin = sourceIP;
    announcement.sdp = *sdpSession;
    announcement.lastSeen = std::chrono::steady_clock::now();
    announcement.isDelete = false;

    // Update cache and notify
    updateAnnouncement(announcement);

    if (discoveryCallback_) {
        discoveryCallback_(*sdpSession);
    }
}

//
// Parse SAP header (RFC 2974)
//
bool SAPListener::parseSAPHeader(const uint8_t* data, size_t length,
                                 bool& isDelete, uint32_t& messageHash, size_t& sdpOffset) {
    if (length < 4) {
        return false;
    }

    // Byte 0: Version (3 bits), A (1), R (1), T (1), E (1), C (1)
    uint8_t byte0 = data[0];
    uint8_t version = (byte0 >> 5) & 0x07;
    bool addressType = (byte0 & 0x10) != 0;  // 0=IPv4, 1=IPv6
    // bool reserved = (byte0 & 0x08) != 0;
    isDelete = (byte0 & 0x04) != 0;          // Message type (T bit)
    bool encrypted = (byte0 & 0x02) != 0;
    bool compressed = (byte0 & 0x01) != 0;

    // We only support SAP version 1, unencrypted, uncompressed, IPv4
    if (version != 1 || encrypted || compressed) {
        return false;
    }

    // Byte 1: Authentication length (in 32-bit words)
    uint8_t authLen = data[1];

    // Bytes 2-3: Message ID hash
    messageHash = (data[2] << 8) | data[3];

    // Calculate offset based on address type
    size_t offset = 4;

    // Originating source address
    if (addressType) {
        offset += 16;  // IPv6
    } else {
        offset += 4;   // IPv4
    }

    // Skip authentication data
    offset += authLen * 4;

    if (offset >= length) {
        return false;
    }

    // Skip payload type (optional MIME type, null-terminated)
    // In AES67, this is typically "application/sdp"
    while (offset < length && data[offset] != 0) {
        offset++;
    }
    offset++;  // Skip null terminator

    sdpOffset = offset;
    return true;
}

//
// Update announcement cache
//
void SAPListener::updateAnnouncement(const SAPAnnouncement& announcement) {
    std::lock_guard<std::mutex> lock(announcementsMutex_);
    announcements_[announcement.messageHash] = announcement;
}

//
// Remove old announcements (timeout after 10 minutes without update)
//
void SAPListener::cleanupOldAnnouncements() {
    std::lock_guard<std::mutex> lock(announcementsMutex_);
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::minutes(10);

    for (auto it = announcements_.begin(); it != announcements_.end(); ) {
        if (now - it->second.lastSeen > timeout) {
            uint32_t hash = it->first;
            it = announcements_.erase(it);

            // Notify deletion
            if (deletionCallback_) {
                deletionCallback_(hash);
            }
        } else {
            ++it;
        }
    }
}

} // namespace AES67
