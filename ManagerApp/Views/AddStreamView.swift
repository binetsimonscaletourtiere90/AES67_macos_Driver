//
// AddStreamView.swift
// AES67 Manager - Build #7
// Sheet for adding new streams manually
//

import SwiftUI

struct AddStreamView: View {
    @EnvironmentObject var driverManager: DriverManager
    @Environment(\.dismiss) var dismiss

    @State private var streamName = ""
    @State private var multicastIP = "239.0.0.1"
    @State private var port = "5004"
    @State private var numChannels = 8
    @State private var sampleRate = 48000
    @State private var encoding = "L24"

    let sampleRates = [44100, 48000, 88200, 96000, 176400, 192000]
    let encodings = ["L16", "L24"]

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("Add AES67 Stream")
                    .font(.title2)
                    .fontWeight(.bold)
                Spacer()
                Button("Cancel") {
                    dismiss()
                }
            }
            .padding()

            Divider()

            // Form
            Form {
                Section("Stream Information") {
                    TextField("Stream Name", text: $streamName)
                        .textFieldStyle(.roundedBorder)
                }

                Section("Network") {
                    TextField("Multicast IP", text: $multicastIP)
                        .textFieldStyle(.roundedBorder)

                    TextField("Port", text: $port)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 100)
                }

                Section("Audio Format") {
                    HStack {
                        Text("Channels")
                        Spacer()
                        Stepper("\(numChannels)", value: $numChannels, in: 1...128)
                            .frame(width: 120)
                    }

                    HStack {
                        Text("Sample Rate")
                        Spacer()
                        Picker("", selection: $sampleRate) {
                            ForEach(sampleRates, id: \.self) { rate in
                                Text("\(rate) Hz").tag(rate)
                            }
                        }
                        .pickerStyle(.menu)
                        .frame(width: 150)
                    }

                    HStack {
                        Text("Encoding")
                        Spacer()
                        Picker("", selection: $encoding) {
                            ForEach(encodings, id: \.self) { enc in
                                Text(enc).tag(enc)
                            }
                        }
                        .pickerStyle(.segmented)
                        .frame(width: 150)
                    }
                }

                Section("Channel Mapping") {
                    Text("Stream will be mapped to first available device channels")
                        .font(.caption)
                        .foregroundColor(.secondary)

                    if driverManager.totalChannelsUsed + numChannels > 128 {
                        Label("Not enough channels available", systemImage: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                            .font(.caption)
                    }
                }
            }
            .formStyle(.grouped)
            .scrollContentBackground(.hidden)

            Divider()

            // Footer
            HStack {
                Spacer()
                Button("Cancel") {
                    dismiss()
                }
                .keyboardShortcut(.cancelAction)

                Button("Add Stream") {
                    addStream()
                }
                .buttonStyle(.borderedProminent)
                .keyboardShortcut(.defaultAction)
                .disabled(!isValid)
            }
            .padding()
        }
        .frame(width: 500, height: 550)
    }

    private var isValid: Bool {
        !streamName.isEmpty &&
        !multicastIP.isEmpty &&
        !port.isEmpty &&
        driverManager.totalChannelsUsed + numChannels <= 128
    }

    private func addStream() {
        guard let portNum = UInt16(port) else { return }

        driverManager.addStream(
            name: streamName,
            multicastIP: multicastIP,
            port: portNum,
            numChannels: UInt16(numChannels),
            sampleRate: UInt32(sampleRate),
            encoding: encoding
        )

        dismiss()
    }
}

#Preview {
    AddStreamView()
        .environmentObject(DriverManager())
}
