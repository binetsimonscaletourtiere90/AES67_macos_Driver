//
// AES67ManagerApp.swift
// AES67 Manager - Build #7
// SwiftUI application for managing AES67 audio streams
//

import SwiftUI

@main
struct AES67ManagerApp: App {
    @StateObject private var driverManager = DriverManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(driverManager)
                .frame(minWidth: 900, minHeight: 600)
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Import SDP File...") {
                    driverManager.importSDPFile()
                }
                .keyboardShortcut("o", modifiers: .command)
            }

            CommandGroup(after: .newItem) {
                Button("Add Stream Manually...") {
                    driverManager.showAddStreamSheet = true
                }
                .keyboardShortcut("n", modifiers: .command)

                Divider()

                Button("Refresh Status") {
                    driverManager.refreshStatus()
                }
                .keyboardShortcut("r", modifiers: .command)
            }
        }

        Settings {
            SettingsView()
                .environmentObject(driverManager)
        }
    }
}
