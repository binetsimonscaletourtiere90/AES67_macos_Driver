//
// SimpleRTP.cpp
// AES67 macOS Driver - Build #8
// Minimal RTP implementation for AES67
//

#include "SimpleRTP.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cmath>

namespace AES67 {
namespace RTP {

//
// RTPSocket Implementation
//

RTPSocket::RTPSocket()
    : sockfd_(-1)
    , isReceiver_(false)
{
    memset(&multicastAddr_, 0, sizeof(multicastAddr_));
}

RTPSocket::~RTPSocket() {
    close();
}

bool RTPSocket::openReceiver(const char* multicastIP, uint16_t port, const char* interfaceIP) {
    // Create UDP socket
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        return false;
    }

    // Allow multiple sockets to bind to same port (for multiple streams)
    int reuse = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    // Bind to port
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(port);

    if (bind(sockfd_, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    // Join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(multicastIP);
    mreq.imr_interface.s_addr = interfaceIP ? inet_addr(interfaceIP) : htonl(INADDR_ANY);

    if (setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    // Set non-blocking mode
    int flags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

    // Increase receive buffer size (4 MB for high channel counts)
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    isReceiver_ = true;

    // Store multicast address for reference
    multicastAddr_.sin_family = AF_INET;
    multicastAddr_.sin_addr.s_addr = inet_addr(multicastIP);
    multicastAddr_.sin_port = htons(port);

    return true;
}

bool RTPSocket::openTransmitter(const char* multicastIP, uint16_t port, const char* interfaceIP) {
    // Create UDP socket
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        return false;
    }

    // Set multicast TTL
    uint8_t ttl = 32;
    if (setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        ::close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    // Set multicast interface
    if (interfaceIP) {
        struct in_addr ifaddr;
        ifaddr.s_addr = inet_addr(interfaceIP);
        if (setsockopt(sockfd_, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof(ifaddr)) < 0) {
            ::close(sockfd_);
            sockfd_ = -1;
            return false;
        }
    }

    // Increase send buffer size
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    isReceiver_ = false;

    // Store destination address
    multicastAddr_.sin_family = AF_INET;
    multicastAddr_.sin_addr.s_addr = inet_addr(multicastIP);
    multicastAddr_.sin_port = htons(port);

    return true;
}

ssize_t RTPSocket::send(const RTPPacket& packet) {
    if (sockfd_ < 0 || isReceiver_) {
        return -1;
    }

    // Prepare header (convert to network byte order)
    RTPHeader header = packet.header;
    header.toNetworkOrder();

    // Send header + payload
    struct iovec iov[2];
    iov[0].iov_base = (void*)&header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = packet.payload;
    iov[1].iov_len = packet.payloadSize;

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &multicastAddr_;
    msg.msg_namelen = sizeof(multicastAddr_);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    return sendmsg(sockfd_, &msg, 0);
}

ssize_t RTPSocket::receive(RTPPacket& packet, uint8_t* buffer, size_t bufferSize) {
    if (sockfd_ < 0 || !isReceiver_) {
        return -1;
    }

    // Receive into buffer
    ssize_t bytesReceived = recvfrom(sockfd_, buffer, bufferSize, 0, nullptr, nullptr);
    if (bytesReceived < (ssize_t)sizeof(RTPHeader)) {
        return -1; // Too small to be valid RTP packet
    }

    // Parse header
    memcpy(&packet.header, buffer, sizeof(RTPHeader));
    packet.header.toHostOrder();

    // Set payload pointer and size
    packet.payload = buffer + sizeof(RTPHeader);
    packet.payloadSize = bytesReceived - sizeof(RTPHeader);

    return bytesReceived;
}

void RTPSocket::close() {
    if (sockfd_ >= 0) {
        // Leave multicast group if receiver
        if (isReceiver_) {
            struct ip_mreq mreq;
            mreq.imr_multiaddr = multicastAddr_.sin_addr;
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            setsockopt(sockfd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        }

        ::close(sockfd_);
        sockfd_ = -1;
    }
}

//
// L16Codec Implementation
//

void L16Codec::encode(const float* samples, size_t numSamples, uint8_t* output) {
    for (size_t i = 0; i < numSamples; i++) {
        // Clamp float to [-1.0, 1.0]
        float sample = std::max(-1.0f, std::min(1.0f, samples[i]));

        // Convert to 16-bit signed integer
        int16_t pcm = static_cast<int16_t>(sample * 32767.0f);

        // Store as big-endian (network byte order)
        output[i * 2 + 0] = (pcm >> 8) & 0xFF;  // MSB
        output[i * 2 + 1] = pcm & 0xFF;         // LSB
    }
}

void L16Codec::decode(const uint8_t* input, size_t numBytes, float* samples) {
    size_t numSamples = numBytes / 2;

    for (size_t i = 0; i < numSamples; i++) {
        // Read big-endian 16-bit value
        int16_t pcm = (input[i * 2 + 0] << 8) | input[i * 2 + 1];

        // Convert to float [-1.0, 1.0]
        samples[i] = pcm / 32768.0f;
    }
}

//
// L24Codec Implementation
//

void L24Codec::encode(const float* samples, size_t numSamples, uint8_t* output) {
    for (size_t i = 0; i < numSamples; i++) {
        // Clamp float to [-1.0, 1.0]
        float sample = std::max(-1.0f, std::min(1.0f, samples[i]));

        // Convert to 24-bit signed integer
        int32_t pcm = static_cast<int32_t>(sample * 8388607.0f); // 2^23 - 1

        // Store as big-endian 24-bit (network byte order)
        output[i * 3 + 0] = (pcm >> 16) & 0xFF;  // MSB
        output[i * 3 + 1] = (pcm >> 8) & 0xFF;   // Middle byte
        output[i * 3 + 2] = pcm & 0xFF;          // LSB
    }
}

void L24Codec::decode(const uint8_t* input, size_t numBytes, float* samples) {
    size_t numSamples = numBytes / 3;

    for (size_t i = 0; i < numSamples; i++) {
        // Read big-endian 24-bit value
        int32_t pcm = (input[i * 3 + 0] << 16) |
                      (input[i * 3 + 1] << 8) |
                      input[i * 3 + 2];

        // Sign extend from 24-bit to 32-bit
        if (pcm & 0x800000) {
            pcm |= 0xFF000000;
        }

        // Convert to float [-1.0, 1.0]
        samples[i] = pcm / 8388608.0f;  // 2^23
    }
}

} // namespace RTP
} // namespace AES67
