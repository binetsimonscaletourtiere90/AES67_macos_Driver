//
// SettingsView.swift
// AES67 Manager - Build #7
// Application settings and preferences
//

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var driverManager: DriverManager
    @AppStorage("autoRefresh") private var autoRefresh = true
    @AppStorage("refreshInterval") private var refreshInterval = 1.0

    var body: some View {
        TabView {
            GeneralSettings()
                .environmentObject(driverManager)
                .tabItem {
                    Label("General", systemImage: "gear")
                }

            DriverSettings()
                .environmentObject(driverManager)
                .tabItem {
                    Label("Driver", systemImage: "waveform")
                }

            AboutSettings()
                .tabItem {
                    Label("About", systemImage: "info.circle")
                }
        }
        .frame(width: 500, height: 400)
    }
}

struct GeneralSettings: View {
    @AppStorage("autoRefresh") private var autoRefresh = true
    @AppStorage("refreshInterval") private var refreshInterval = 1.0

    var body: some View {
        Form {
            Section("Status Updates") {
                Toggle("Auto-refresh stream status", isOn: $autoRefresh)

                if autoRefresh {
                    HStack {
                        Text("Refresh interval")
                        Spacer()
                        Slider(value: $refreshInterval, in: 0.5...5.0, step: 0.5)
                            .frame(width: 200)
                        Text("\(refreshInterval, specifier: "%.1f")s")
                            .frame(width: 40)
                    }
                }
            }

            Section("Appearance") {
                Toggle("Show detailed statistics", isOn: .constant(true))
                Toggle("Show channel visualization", isOn: .constant(true))
            }
        }
        .formStyle(.grouped)
        .padding()
    }
}

struct DriverSettings: View {
    @EnvironmentObject var driverManager: DriverManager

    var body: some View {
        Form {
            Section("Driver Status") {
                HStack {
                    Text("Status")
                    Spacer()
                    StatusBadge(isConnected: driverManager.isDriverLoaded)
                }

                HStack {
                    Text("Version")
                    Spacer()
                    Text("1.0.7")
                        .foregroundColor(.secondary)
                }

                HStack {
                    Text("Location")
                    Spacer()
                    Text("/Library/Audio/Plug-Ins/HAL/")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Section("Actions") {
                Button("Restart Core Audio") {
                    driverManager.restartCoreAudio()
                }

                Button("Check Driver Installation") {
                    driverManager.checkDriverInstallation()
                }
            }
        }
        .formStyle(.grouped)
        .padding()
    }
}

struct AboutSettings: View {
    var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "waveform.circle.fill")
                .font(.system(size: 64))
                .foregroundColor(.blue)

            Text("AES67 Audio Driver")
                .font(.title)
                .fontWeight(.bold)

            Text("Version 1.0.7 (Build #7)")
                .foregroundColor(.secondary)

            Divider()
                .padding(.horizontal, 60)

            VStack(alignment: .leading, spacing: 12) {
                Text("Professional AES67/RAVENNA/Dante audio driver for macOS")
                    .multilineTextAlignment(.center)

                Text("Features:")
                    .fontWeight(.semibold)

                VStack(alignment: .leading, spacing: 4) {
                    Label("128 audio channels", systemImage: "speaker.wave.3")
                    Label("L16 and L24 encoding", systemImage: "waveform")
                    Label("PTP synchronization", systemImage: "clock")
                    Label("Multiple simultaneous streams", systemImage: "network")
                }
                .font(.caption)
            }
            .padding()

            Spacer()

            Text("© 2025 AES67 Driver Project")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

#Preview {
    SettingsView()
        .environmentObject(DriverManager())
}
