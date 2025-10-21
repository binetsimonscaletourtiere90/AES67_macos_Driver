# Building AES67 macOS Driver - Build #1

This document provides complete instructions for building the AES67 driver on macOS.

## Prerequisites

### System Requirements
- **macOS**: 12.0 (Monterey) or later
- **Xcode**: 14.0 or later with Command Line Tools
- **Homebrew**: Package manager for macOS

### Install Homebrew
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## Installing Dependencies

### 1. Install Build Tools and Libraries
```bash
# Install CMake and other build tools
brew install cmake pkg-config doxygen

# Install oRTP (RTP/RTCP stack)
brew install ortp

# Optional: Install git if not already installed
brew install git
```

### 2. Install libASPL (AudioServerPlugIn Library)
```bash
# Clone libASPL
cd ~/Downloads
git clone https://github.com/gavv/libASPL.git
cd libASPL

# Build and install
make
sudo make install

# Verify installation
ls /usr/local/include/aspl/
```

### 3. Build ptpd (PTP Daemon)
```bash
# Clone ptpd
cd ~/Downloads
git clone https://github.com/ptpd/ptpd.git
cd ptpd

# Configure and build
./autogen.sh
./configure --prefix=/usr/local
make

# Install
sudo make install

# Verify installation
which ptpd
```

## Building the Driver

### Option 1: Command Line Build (CMake)

```bash
# Navigate to project directory
cd /path/to/AES67_macos_Driver

# Create build directory
mkdir build
cd build

# Generate build system
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release

# The driver bundle will be at: build/AES67.driver
```

### Option 2: Xcode Build (Recommended)

```bash
# Navigate to project directory
cd /path/to/AES67_macos_Driver

# Create Xcode project
mkdir build
cd build
cmake .. -G Xcode

# Open in Xcode
open AES67Driver.xcodeproj
```

In Xcode:
1. Select scheme: **AES67Driver**
2. Select destination: **My Mac**
3. Product → Build (⌘B)

The built driver will be in: `build/Release/AES67.driver`

## Building the Manager App

The SwiftUI management application is built separately:

```bash
cd ManagerApp
open AES67Manager.xcodeproj
```

In Xcode:
1. Select scheme: **AES67 Manager**
2. Product → Build (⌘B)
3. Product → Archive (for distribution)

## Installation

### Install Driver

```bash
# Copy driver to system location
sudo cp -r build/Release/AES67.driver /Library/Audio/Plug-Ins/HAL/

# Set proper permissions
sudo chmod -R 755 /Library/Audio/Plug-Ins/HAL/AES67.driver

# Restart Core Audio daemon
sudo killall coreaudiod

# Verify installation
ls -l /Library/Audio/Plug-Ins/HAL/AES67.driver
```

### Install Manager App

```bash
# Copy to Applications
cp -r build/Release/AES67\ Manager.app /Applications/

# Verify installation
open -a "AES67 Manager"
```

## Verification

### 1. Check Driver Loaded

```bash
# List audio devices (should show AES67 Device)
system_profiler SPAudioDataType
```

### 2. Open Audio MIDI Setup

```bash
open "/Applications/Utilities/Audio MIDI Setup.app"
```

You should see "AES67 Device" in the device list.

### 3. Test with Manager App

```bash
open -a "AES67 Manager"
```

The app should show:
- Device status
- 128 available channels
- Current sample rate and buffer size

## Development Build

For development with debugging:

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -G Xcode
open AES67Driver.xcodeproj
```

In Xcode:
1. Set breakpoints as needed
2. Product → Scheme → Edit Scheme
3. Run → Info → Executable: Choose "Audio MIDI Setup.app"
4. Product → Run (⌘R)

This will launch Audio MIDI Setup with the debugger attached.

## Code Signing

For distribution, you need to sign the driver:

### Development Signing (Ad-hoc)

```bash
# Sign with ad-hoc signature
codesign --force --deep --sign - /Library/Audio/Plug-Ins/HAL/AES67.driver
```

### Distribution Signing

```bash
# Sign with Developer ID
codesign --force --deep \
    --sign "Developer ID Application: Your Name (TEAM_ID)" \
    --options runtime \
    /Library/Audio/Plug-Ins/HAL/AES67.driver

# Verify signature
codesign -vvv --deep /Library/Audio/Plug-Ins/HAL/AES67.driver
spctl --assess --type install /Library/Audio/Plug-Ins/HAL/AES67.driver
```

### Notarization (Required for macOS 10.15+)

```bash
# Create ZIP for notarization
cd /Library/Audio/Plug-Ins/HAL
zip -r ~/AES67.driver.zip AES67.driver

# Submit for notarization
xcrun notarytool submit ~/AES67.driver.zip \
    --apple-id "your@email.com" \
    --team-id "TEAM_ID" \
    --password "app-specific-password" \
    --wait

# Staple the ticket
xcrun stapler staple /Library/Audio/Plug-Ins/HAL/AES67.driver

# Verify notarization
xcrun stapler validate /Library/Audio/Plug-Ins/HAL/AES67.driver
```

## Troubleshooting

### Driver Not Loading

```bash
# Check Console.app for errors
open -a Console

# Filter for: coreaudiod
# Look for errors related to AES67.driver
```

### Permission Issues

```bash
# Reset permissions
sudo chown -R root:wheel /Library/Audio/Plug-Ins/HAL/AES67.driver
sudo chmod -R 755 /Library/Audio/Plug-Ins/HAL/AES67.driver
sudo killall coreaudiod
```

### Build Errors

**libASPL not found:**
```bash
# Verify installation
ls /usr/local/include/aspl/aspl.hpp

# If not found, reinstall libASPL
cd ~/Downloads/libASPL
sudo make install
```

**oRTP not found:**
```bash
# Reinstall oRTP
brew reinstall ortp

# Verify pkg-config can find it
pkg-config --cflags --libs ortp
```

**Xcode Command Line Tools missing:**
```bash
xcode-select --install
```

## Uninstallation

```bash
# Remove driver
sudo rm -rf /Library/Audio/Plug-Ins/HAL/AES67.driver

# Remove app
rm -rf /Applications/AES67\ Manager.app

# Remove configuration
rm -rf ~/Library/Application\ Support/AES67Driver
rm -rf /Library/Application\ Support/AES67Driver

# Restart Core Audio
sudo killall coreaudiod
```

## Build Artifacts

After building, you should have:

```
build/
├── AES67.driver/              # AudioServerPlugIn bundle
│   └── Contents/
│       ├── MacOS/
│       │   └── AES67Driver    # Binary
│       ├── Info.plist         # Bundle metadata
│       └── Resources/         # Resources
├── AES67 Manager.app/         # Management application
└── Tests/                     # Unit tests (if built)
```

## Performance Notes

### Optimized Build Flags

For best performance, use Release build with optimizations:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_CXX_FLAGS_RELEASE="-O3 -march=native -DNDEBUG"
```

### Profile Guided Optimization (Advanced)

```bash
# Step 1: Build with instrumentation
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_CXX_FLAGS="-fprofile-generate"
cmake --build .

# Step 2: Run typical workload (record profiling data)
# Use the driver with your DAW for 10-15 minutes

# Step 3: Rebuild with profiling data
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_CXX_FLAGS="-fprofile-use"
cmake --build .
```

## Next Steps

After building:
1. Read [README.md](README.md) for usage instructions
2. Check [Docs/](Docs/) for detailed documentation
3. Review [Examples/](Docs/Examples/) for SDP file examples
4. Test with your Riedel system or other AES67 devices

## Support

For build issues:
- Check GitHub Issues
- Review Console.app logs
- Enable verbose CMake output: `cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON`
- Check Xcode build logs

---

**Build Number**: 1
**Last Updated**: 2025-10-19
