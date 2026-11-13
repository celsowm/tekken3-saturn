// Send timestamped keyboard events to one Mednafen process. SDL ignores keys while its window is in
// the background, so each key first brings that process to the front; other apps get no keys.
import ApplicationServices
import Foundation

guard CommandLine.arguments.count == 3, let pid = Int32(CommandLine.arguments[1]),
      AXIsProcessTrusted() else {
    fputs("Usage: mednafen_input_script.swift PID SCRIPT (requires accessibility access)\n", stderr)
    exit(1)
}
let source = try String(contentsOfFile: CommandLine.arguments[2], encoding: .utf8)
var events: [(at: Double, key: UInt16, down: Bool)] = []
for line in source.split(separator: "\n") {
    let parts = line.split(separator: " ")
    if parts.isEmpty || parts[0].hasPrefix("#") { continue }
    guard parts.count == 3, let at = Double(parts[0]), let key = UInt16(parts[1]),
          let hold = Double(parts[2]), at >= 0, hold > 0 else {
        fputs("Invalid input script line: \(line)\n", stderr); exit(1)
    }
    events.append((at, key, true))
    events.append((at + hold, key, false))
}
// Schedule releases separately: a long direction hold must not delay the
// attacks requested during it. Release first when two events share a time.
events.sort { $0.at == $1.at ? (!$0.down && $1.down) : $0.at < $1.at }
let started = Date()
for event in events {
    let wait = event.at - Date().timeIntervalSince(started)
    if wait > 0 { Thread.sleep(forTimeInterval: wait) }
    guard let keyEvent = CGEvent(keyboardEventSource: nil, virtualKey: event.key,
                                 keyDown: event.down) else { exit(1) }
    // SDL's command-line process is not necessarily an NSRunningApplication.
    // System Events identifies and focuses the exact Unix process instead.
    // Another window can take the front during a long take (a terminal that
    // raises itself, for one): focus again, and skip only this key if it fails.
    var focused: String? = nil
    for attempt in 0..<3 {
        if attempt > 0 { Thread.sleep(forTimeInterval: 0.05) }
        let focus = Process()
        let output = Pipe()
        focus.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        focus.arguments = ["-e", "tell application \"System Events\" to set frontmost of first process whose unix id is \(pid) to true",
                           "-e", "tell application \"System Events\" to get unix id of first process whose frontmost is true"]
        focus.standardOutput = output
        try focus.run()
        focus.waitUntilExit()
        focused = String(data: output.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if focus.terminationStatus == 0, focused == String(pid) { break }
        focused = nil
    }
    guard focused != nil else {
        fputs("Mednafen focus failed at \(event.at) s: expected PID \(pid); key \(event.key) skipped\n", stderr)
        continue
    }
    // SDL polls the keyboard state. A process-targeted Quartz event does not
    // update that state; post through HID after checking the focused process.
    keyEvent.post(tap: .cghidEventTap)
}
