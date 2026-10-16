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
//  quit -> copy + verify the copy -> OBS still quit? -> atomic replace ->
//  done (optionally open OBS).
//
//  Safety:
//    * The plug-in must pass code-signature validation. When this installer is
//      signed with a Team ID (Developer ID release), the plug-in must be signed
//      by the same team; ad-hoc builds (CI) only require a valid signature.
//    * Writes only inside the user's home folder; no admin rights, no TCC.
//    * OBS is never quit by the installer; the user is asked to quit it.
//
//  Headless mode for CI:  <exe> --headless --dest <folder>
//    Same find / verify / install steps without UI (exit codes: headless()).
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
    var isSignature = false
    var errorDescription: String? { message }

    static var signature: InstallError { InstallError(message: L("error.signature"), isSignature: true) }
    static func copy(_ error: Error) -> InstallError {
        InstallError(message: String(format: L("error.copy"), error.localizedDescription))
    }
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

    // Next to this app first. When Gatekeeper runs the app from a randomized
    // read-only copy (App Translocation), the sibling is missing, so the
    // mounted volumes are searched too. Only the plug-in built together with
    // this installer (same version) qualifies: an older image that is still
    // mounted must never be installed in its place.
    static func locatePlugin() -> URL? {
        guard let mine = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String else {
            return nil
        }
        let fm = FileManager.default
        var candidates = [Bundle.main.bundleURL.deletingLastPathComponent().appendingPathComponent(kPluginName)]
        let volumes = fm.mountedVolumeURLs(includingResourceValuesForKeys: nil, options: [.skipHiddenVolumes]) ?? []
        candidates += volumes.map { $0.appendingPathComponent(kPluginName) }
        return candidates.first {
            let plist = info(at: $0)
            return plist?["CFBundleIdentifier"] as? String == kPluginID
                && plist?["CFBundleShortVersionString"] as? String == mine
        }
    }

    // What the plug-in's signature must satisfy: the same Team ID as this
    // installer. Only an ad-hoc signed installer (CI and local builds) relaxes
    // that to "any valid signature". Any Security API failure rejects.
    static func pluginRequirement() throws -> SecRequirement? {
        var code: SecStaticCode?
        guard SecStaticCodeCreateWithPath(Bundle.main.bundleURL as CFURL, [], &code) == errSecSuccess,
              let code else { throw InstallError.signature }
        var info: CFDictionary?
        guard SecCodeCopySigningInformation(code, SecCSFlags(rawValue: kSecCSSigningInformation), &info) == errSecSuccess,
              let dict = info as? [String: Any] else { throw InstallError.signature }
        if let team = dict[kSecCodeInfoTeamIdentifier as String] as? String {
            guard !team.isEmpty, team.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber) }) else {
                throw InstallError.signature
            }
            let text = "anchor apple generic and certificate leaf[subject.OU] = \"\(team)\""
            var requirement: SecRequirement?
            guard SecRequirementCreateWithString(text as CFString, [], &requirement) == errSecSuccess,
                  let requirement else { throw InstallError.signature }
            return requirement
        }
        let flags = (dict[kSecCodeInfoFlags as String] as? NSNumber)?.uint32Value ?? 0
        guard flags & SecCodeSignatureFlags.adhoc.rawValue != 0 else { throw InstallError.signature }
        return nil
    }

    static func verify(_ plugin: URL) throws {
        let requirement = try pluginRequirement()
        var code: SecStaticCode?
        guard SecStaticCodeCreateWithPath(plugin as CFURL, [], &code) == errSecSuccess, let code,
              SecStaticCodeCheckValidity(code, SecCSFlags(rawValue: kSecCSCheckAllArchitectures), requirement) == errSecSuccess
        else { throw InstallError.signature }
    }

    // A verified copy waiting in a scratch folder on the destination volume.
    struct Staged {
        let scratch: URL
        let bundle: URL
        let target: URL

        func discard() {
            try? FileManager.default.removeItem(at: scratch)
        }
    }

    // Step 1: copy into a scratch folder and verify the COPY (the source may
    // have changed while the dialogs were open). Nothing where OBS looks has
    // been touched yet.
    static func stage(_ plugin: URL, into folder: URL) throws -> Staged {
        let fm = FileManager.default
        let scratch: URL
        do {
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            scratch = try fm.url(for: .itemReplacementDirectory, in: .userDomainMask,
                                 appropriateFor: folder, create: true)
        } catch {
            throw InstallError.copy(error)
        }
        let staged = Staged(scratch: scratch,
                            bundle: scratch.appendingPathComponent(kPluginName, isDirectory: true),
                            target: folder.appendingPathComponent(kPluginName, isDirectory: true))
        do {
            try fm.copyItem(at: plugin, to: staged.bundle)
            try verify(staged.bundle)
        } catch let error as InstallError {
            staged.discard()
            throw error
        } catch {
            staged.discard()
            throw InstallError.copy(error)
        }
        clearQuarantine(staged.bundle)
        return staged
    }

    // Step 2: swap the verified copy in (atomic replace on the same volume).
    @discardableResult
    static func commit(_ staged: Staged) throws -> URL {
        defer { staged.discard() }
        let fm = FileManager.default
        do {
            if fm.fileExists(atPath: staged.target.path) {
                _ = try fm.replaceItemAt(staged.target, withItemAt: staged.bundle)
            } else {
                try fm.moveItem(at: staged.bundle, to: staged.target)
            }
        } catch {
            throw InstallError.copy(error)
        }
        return staged.target
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

        let staged: Installer.Staged
        do {
            staged = try Installer.stage(plugin, into: Installer.defaultDestination)
        } catch {
            fail(error.localizedDescription)
            return
        }
        // OBS may have been started while the copy ran.
        while obsRunning {
            guard ask(L("running.title"), L("running.text"), style: .warning,
                      action: L("retry"), cancel: L("cancel")) else {
                staged.discard()
                return
            }
        }
        do {
            try Installer.commit(staged)
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

    // Exit codes: 0 installed, 1 copy failed, 2 plug-in not found,
    // 3 signature rejected, 64 usage (--dest is required so a test run can
    // never touch the real OBS plug-ins folder).
    static func headless(_ args: [String]) -> Int32 {
        func complain(_ text: String) {
            FileHandle.standardError.write(Data("error: \(text)\n".utf8))
        }
        guard let i = args.firstIndex(of: "--dest"), i + 1 < args.count,
              !args[i + 1].isEmpty, !args[i + 1].hasPrefix("-") else {
            complain("usage: --headless --dest <folder>")
            return 64
        }
        let dest = URL(fileURLWithPath: args[i + 1], isDirectory: true)
        guard let plugin = Installer.locatePlugin() else {
            complain("\(kPluginName) (this installer's version) not found")
            return 2
        }
        do {
            try Installer.verify(plugin)
            let target = try Installer.commit(Installer.stage(plugin, into: dest))
            print("installed \(plugin.path) -> \(target.path)")
            return 0
        } catch let error as InstallError {
            complain(error.message)
            return error.isSignature ? 3 : 1
        } catch {
            complain(error.localizedDescription)
            return 1
        }
    }
}
