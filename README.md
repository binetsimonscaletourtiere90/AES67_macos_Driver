# AES67 macOS Audio Driver

A professional-grade virtual audio driver for macOS that brings AES67/RAVENNA/Dante network audio support to your Mac. This driver creates a 128-channel virtual audio device that integrates seamlessly with all Core Audio applications.

## Overview

This driver enables macOS applications to send and receive audio over IP networks using the AES67 standard. Whether you're working with broadcast equipment, live sound systems, or professional recording setups, this driver provides a reliable bridge between your Mac and AES67-compatible devices.

### Key Features

- **128 Channels In/Out** - Full 128-channel bidirectional audio support
- **Multiple Sample Rates** - 44.1kHz to 384kHz support for professional workflows
- **Network Audio** - Built-in AES67, RAVENNA, and Dante compatibility
- **Low Latency** - Optimized ring buffers with 2ms default latency
- **Universal Integration** - Works with any Core Audio application (Logic Pro, Pro Tools, Ableton Live, QLab, etc.)
- **Native Apple Silicon** - Optimized for M1/M2/M3 Macs
- **SDP Import/Export** - Easy stream configuration via SDP files

## What's Included

### Audio Driver
The core AudioServerPlugIn driver that appears as "AES67 Device" in your system. Built using modern C++17 with the libASPL framework for rock-solid stability.

### Manager Application
A native SwiftUI app for managing your AES67 streams:
- Add/remove network audio streams
- Configure channel mappings
- Import/export SDP session files
- Monitor stream status
- View statistics

## Architecture

```
AES67Driver/
├── Driver/                  # AudioServerPlugIn (libASPL)
│   ├── AES67Device          # 128-channel Core Audio device
│   ├── AES67IOHandler       # RT-safe audio IO
│   └── SDPParser            # SDP file import/export
├── NetworkEngine/           # Network audio processing
│   ├── RTP/
│   │   ├── RTPReceiver      # oRTP receiver with L16/L24 decoding
│   │   └── RTPTransmitter   # oRTP transmitter
│   ├── PTP/
│   │   ├── PTPClock         # Per-domain PTP clock
│   │   └── PTPClockManager  # Multi-domain coordinator
│   ├── Discovery/
│   │   ├── SAPListener      # SAP discovery
│   │   └── RTSPClient       # RTSP client
│   ├── StreamManager        # Unified stream management
│   └── StreamChannelMapper  # Stream→Device channel mapping
├── Shared/                  # Common components
│   ├── RingBuffer.hpp       # Lock-free SPSC ring buffer
│   ├── Types.h              # Common types and structs
│   └── Config.hpp           # Configuration management
├── ManagerApp/              # SwiftUI management application
│   ├── Views/
│   │   ├── StreamBrowserView       # Stream discovery/management
│   │   ├── ChannelMappingView      # Visual channel mapper
│   │   ├── PTPStatusView           # PTP domain status
│   │   └── DeviceSettingsView      # Driver settings
│   └── Models/
│       └── DriverState             # App state management
├── Tests/                   # Unit and integration tests
└── Installer/               # macOS package installer
```

## System Requirements

- macOS 13.0 (Ventura) or later
- Apple Silicon (M1/M2/M3) Mac
- Network connection for AES67 audio streams

## Installation

1. Download the latest release package: `AES67Driver-Complete-[version]-arm64.pkg`
2. Double-click to install (requires admin password)
3. The driver installs automatically - no restart required
4. Look for "AES67 Device" in System Settings → Sound

**Note:** The driver will appear in Audio MIDI Setup and all Core Audio applications immediately after installation.

## Quick Start

### Setting Up Your First Stream

1. **Launch AES67 Manager** (installed in Applications folder)
2. **Import an SDP file** or manually configure a stream:
   - Stream name
   - Multicast IP address
   - Port number
   - Channel count
   - Sample rate
3. **Map channels** to your device channels (0-127)
4. **Select "AES67 Device"** as your audio input/output in your DAW or application

### Using with QLab

1. Open QLab preferences → Audio
2. Select "AES67 Device" as your output device
3. Configure routing in Audio Patch settings
4. All 128 channels are available for routing

### Using with Logic Pro / Pro Tools

1. Open Preferences → Audio
2. Select "AES67 Device" as your audio device
3. Access all 128 channels in your I/O configuration

## Technical Architecture

### Core Components

- **AudioServerPlugIn Driver** - macOS HAL plugin using libASPL
- **RTP Engine** - Network audio packet handling via oRTP library
- **Channel Mapper** - Flexible routing between streams and device channels
- **Ring Buffers** - Lock-free SPSC buffers for real-time audio
- **PTP Sync** - Precision time synchronization (IEEE 1588)

### Network Audio Support

- **AES67** - Audio Engineering Society standard for audio over IP
- **RAVENNA** - Merging Technologies network audio protocol
- **Dante** - Audinate's media networking technology
- **Sample Formats** - L16 (16-bit) and L24 (24-bit) linear PCM

### Performance

- **Latency**: ~2ms default (configurable via ring buffer size)
- **Buffer Sizes**: 16 to 480 samples
- **Sample Rates**: 44.1kHz to 384kHz
- **CPU Usage**: Minimal - optimized batch processing
- **Jitter Handling**: Adaptive buffering for network variations

## Building from Source

### Prerequisites

```bash
# Install dependencies via Homebrew
brew install cmake ortp

# Install libASPL
brew tap gavv/gavv
brew install libaspl
```

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/yourusername/AES67_macos_Driver.git
cd AES67_macos_Driver

# Configure and build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
make -j

# The driver will be built at: build/AES67Driver.driver
```

### Creating an Installer

See the detailed build instructions in the repository for creating a complete installer package with bundled dependencies.

## Troubleshooting

### Driver Not Appearing

If the driver doesn't show up after installation:

```bash
# Check if driver is installed
ls -la /Library/Audio/Plug-Ins/HAL/AES67Driver.driver

# Restart Core Audio
sudo killall coreaudiod

# Check debug log
cat /tmp/aes67driver_debug.log
```

### No Audio Output

1. **Check System Settings** - Ensure "AES67 Device" is selected
2. **Verify Stream Configuration** - Check Manager app for active streams
3. **Network Connection** - Confirm multicast routing is enabled
4. **Firewall Settings** - Allow UDP traffic on configured ports

### Uninstalling

```bash
# Remove driver
sudo rm -rf /Library/Audio/Plug-Ins/HAL/AES67Driver.driver

# Remove manager app
sudo rm -rf /Applications/AES67Manager.app

# Restart Core Audio
sudo killall coreaudiod
```

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues for bugs and feature requests.

### Development Guidelines

- Follow C++17 standards
- Use RAII for resource management
- Maintain lock-free audio thread
- Add unit tests for new features
- Update documentation

## License

This project is released under the MIT License. See LICENSE file for details.

Component licenses:
- **libASPL**: MIT License
- **oRTP**: LGPL 2.1

## Acknowledgments

- **libASPL** - Modern C++ AudioServerPlugIn framework by [gavv](https://github.com/gavv/libASPL)
- **oRTP** - RTP library by Belledonne Communications
- **AES67 Standard** - Audio Engineering Society

## Support

For issues, questions, or feature requests, please open an issue on GitHub.

---

**Built with ❤️ for the professional audio community**

*Version 1.0.0 - Build #8*
