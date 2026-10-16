// =============================================================================
//  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
//  packaging/macos/installer/Installer.swift  -  "MODEL 600 Installer.app"
//
//  Copies model600.plugin from the disk image into the only folder OBS Studio
//  (28+) loads third-party .plugin bundles from on macOS:
//    ~/Library/Application Support/obs-studio/plugins
//  (obs-studio frontend/widgets/OBSBasic.cpp, AddExtraModulePaths).
//
//  UI is the system alert only (NSAlert), ja / en via Localizable.strings.
//  Flow: find plug-in -> check its signature -> confirm -> wait until OBS is
//  quit -> stage + atomic replace -> done (optionally open OBS).
//
//  Safety:
//    * The plug-in must pass code-signature validation. When this installer is
//      signed with a Team ID (Developer ID release), the plug-in must be signed
//      by the same team; ad-hoc builds (CI) only require a valid signature.
//    * Writes only inside the user's home folder; no admin rights, no TCC.
//    * OBS is never quit by the installer; the user is asked to quit it.
//
//  Headless mode for CI:  <exe> --headless --dest <folder>
//    Same find / verify / install steps without UI. Exit 0 on success.
// =============================================================================
import AppKit
import Security

private let kPluginName = "model600.plugin"
private let kPluginID = "com.seventhwell.model600"
private let kOBSID = "com.obsproject.obs-studio"

private func L(_ key: String) -> String {
    NSLocalizedString(key, comment: "")
}

struct InstallError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

// -----------------------------------------------------------------------------
//  Installer: everything except UI (also used by --headless).
// -----------------------------------------------------------------------------
enum Installer {
    static var defaultDestination: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/obs-studio/plugins", isDirectory: true)
    }

    static func info(at bundle: URL) -> [String: Any]? {
        NSDictionary(contentsOf: bundle.appendingPathComponent("Contents/Info.plist")) as? [String: Any]
    }

    static func version(at bundle: URL) -> String? {
        info(at: bundle)?["CFBundleShortVersionString"] as? String
    }

    // Next to this app first. When Gatekeeper runs the app from a randomized
    // read-only copy (App Translocation), the sibling is missing, so the
    // mounted volumes are searched too. A copy with this installer's version
    // wins; otherwise the first MODEL 600 bundle found.
    static func locatePlugin() -> URL? {
        let fm = FileManager.default
        var candidates = [Bundle.main.bundleURL.deletingLastPathComponent().appendingPathComponent(kPluginName)]
        let volumes = fm.mountedVolumeURLs(includingResourceValuesForKeys: nil, options: [.skipHiddenVolumes]) ?? []
        candidates += volumes.map { $0.appendingPathComponent(kPluginName) }
        let ours = candidates.filter { info(at: $0)?["CFBundleIdentifier"] as? String == kPluginID }
        let mine = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String
        return ours.first { version(at: $0) == mine } ?? ours.first
    }

    static func teamID(of url: URL) -> String? {
        var code: SecStaticCode?
        guard SecStaticCodeCreateWithPath(url as CFURL, [], &code) == errSecSuccess, let code else { return nil }
        var info: CFDictionary?
        guard SecCodeCopySigningInformation(code, SecCSFlags(rawValue: kSecCSSigningInformation), &info) == errSecSuccess,
              let dict = info as? [String: Any] else { return nil }
        return dict[kSecCodeInfoTeamIdentifier as String] as? String
    }

    static func verify(_ plugin: URL) throws {
        var code: SecStaticCode?
        guard SecStaticCodeCreateWithPath(plugin as CFURL, [], &code) == errSecSuccess, let code else {
            throw InstallError(message: L("error.signature"))
        }
        var requirement: SecRequirement?
        if let team = teamID(of: Bundle.main.bundleURL) {
            guard !team.isEmpty, team.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber) }) else {
                throw InstallError(message: L("error.signature"))
            }
            let text = "anchor apple generic and certificate leaf[subject.OU] = \"\(team)\""
            guard SecRequirementCreateWithString(text as CFString, [], &requirement) == errSecSuccess else {
                throw InstallError(message: L("error.signature"))
            }
        }
        let flags = SecCSFlags(rawValue: kSecCSCheckAllArchitectures)
        guard SecStaticCodeCheckValidity(code, flags, requirement) == errSecSuccess else {
            throw InstallError(message: L("error.signature"))
        }
    }

    // Copies into a scratch folder on the same volume, then swaps it in, so a
    // failure never leaves a half-copied bundle where OBS looks.
    @discardableResult
    static func install(_ plugin: URL, into folder: URL) throws -> URL {
        let fm = FileManager.default
        let target = folder.appendingPathComponent(kPluginName, isDirectory: true)
        do {
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            let scratch = try fm.url(for: .itemReplacementDirectory, in: .userDomainMask,
                                     appropriateFor: folder, create: true)
            defer { try? fm.removeItem(at: scratch) }
            let staged = scratch.appendingPathComponent(kPluginName, isDirectory: true)
            try fm.copyItem(at: plugin, to: staged)
            clearQuarantine(staged)
            if fm.fileExists(atPath: target.path) {
                _ = try fm.replaceItemAt(target, withItemAt: staged)
            } else {
                try fm.moveItem(at: staged, to: target)
            }
        } catch {
            throw InstallError(message: String(format: L("error.copy"), error.localizedDescription))
        }
        return target
    }

    // The signature was checked above; drop the download quarantine like the
    // manual installer (install_mac.command) does.
    static func clearQuarantine(_ root: URL) {
        var paths = [root.path]
        if let walker = FileManager.default.enumerator(atPath: root.path) {
            for case let rel as String in walker {
                paths.append(root.appendingPathComponent(rel).path)
            }
        }
        for path in paths {
            _ = removexattr(path, "com.apple.quarantine", XATTR_NOFOLLOW)
        }
    }
}

// -----------------------------------------------------------------------------
//  Alerts.
// -----------------------------------------------------------------------------
@MainActor
final class Flow {
    // Returns true for the default (rightmost) button.
    private func ask(_ title: String, _ text: String, style: NSAlert.Style,
                     action: String, cancel: String? = nil) -> Bool {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = text
        alert.alertStyle = style
        alert.addButton(withTitle: action)
        if let cancel {
            alert.addButton(withTitle: cancel).keyEquivalent = "\u{1b}"
        }
        NSApp.activate(ignoringOtherApps: true)
        return alert.runModal() == .alertFirstButtonReturn
    }

    private func fail(_ reason: String) {
        _ = ask(L("fail.title"), reason + "\n\n" + L("fail.hint"), style: .warning, action: L("close"))
    }

    private var obsRunning: Bool {
        !NSRunningApplication.runningApplications(withBundleIdentifier: kOBSID).isEmpty
    }

    func run() {
        guard let plugin = Installer.locatePlugin() else {
            fail(L("error.notFound"))
            return
        }
        do {
            try Installer.verify(plugin)
        } catch {
            fail(error.localizedDescription)
            return
        }

        let obsApp = NSWorkspace.shared.urlForApplication(withBundleIdentifier: kOBSID)
        if obsApp == nil {
            guard ask(L("noOBS.title"), L("noOBS.text"), style: .warning,
                      action: L("continue"), cancel: L("cancel")) else { return }
        }
        guard ask(L("confirm.title"), L("confirm.text"), style: .informational,
                  action: L("install"), cancel: L("cancel")) else { return }
        while obsRunning {
            guard ask(L("running.title"), L("running.text"), style: .warning,
                      action: L("retry"), cancel: L("cancel")) else { return }
        }

        do {
            try Installer.install(plugin, into: Installer.defaultDestination)
        } catch {
            fail(error.localizedDescription)
            return
        }

        guard let obsApp else {
            _ = ask(L("done.title"), L("done.text"), style: .informational, action: L("done"))
            return
        }
        if ask(L("done.title"), L("done.text"), style: .informational, action: L("openOBS"), cancel: L("done")) {
            open(obsApp)
        }
    }

    // Waits (up to 10 s) for Launch Services so the launch is not cut short
    // by this app quitting right after.
    private func open(_ app: URL) {
        let done = DispatchSemaphore(value: 0)
        NSWorkspace.shared.openApplication(at: app, configuration: NSWorkspace.OpenConfiguration()) { _, _ in
            done.signal()
        }
        _ = done.wait(timeout: .now() + 10)
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        Flow().run()
        NSApp.terminate(nil)
    }
}

// -----------------------------------------------------------------------------
//  Entry point (built with -parse-as-library).
// -----------------------------------------------------------------------------
@main
struct InstallerMain {
    @MainActor
    static func main() {
        let args = CommandLine.arguments
        if args.contains("--headless") {
            exit(headless(args))
        }
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        _ = app.setActivationPolicy(.regular)
        app.run()
    }

    static func headless(_ args: [String]) -> Int32 {
        var dest = Installer.defaultDestination
        if let i = args.firstIndex(of: "--dest"), i + 1 < args.count {
            dest = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        }
        guard let plugin = Installer.locatePlugin() else {
            FileHandle.standardError.write(Data("error: \(kPluginName) not found\n".utf8))
            return 2
        }
        do {
            try Installer.verify(plugin)
            let target = try Installer.install(plugin, into: dest)
            print("installed \(plugin.path) -> \(target.path)")
            return 0
        } catch {
            FileHandle.standardError.write(Data("error: \(error.localizedDescription)\n".utf8))
            return 1
        }
    }
}
