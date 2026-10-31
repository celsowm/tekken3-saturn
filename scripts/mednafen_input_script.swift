// Send timestamped keyboard events to one Mednafen process without changing focus.
import ApplicationServices
import Foundation

guard CommandLine.arguments.count == 3, let pid = Int32(CommandLine.arguments[1]),
      AXIsProcessTrusted() else {
    fputs("Usage: mednafen_input_script.swift PID SCRIPT (requires accessibility access)\n", stderr)
    exit(1)
}
let source = try String(contentsOfFile: CommandLine.arguments[2], encoding: .utf8)
let started = Date()
for line in source.split(separator: "\n") {
    let parts = line.split(separator: " ")
    if parts.isEmpty || parts[0].hasPrefix("#") { continue }
    guard parts.count == 3, let at = Double(parts[0]), let key = UInt16(parts[1]),
          let hold = Double(parts[2]), at >= 0, hold > 0 else {
        fputs("Invalid input script line: \(line)\n", stderr); exit(1)
    }
    let wait = at - Date().timeIntervalSince(started)
    if wait > 0 { Thread.sleep(forTimeInterval: wait) }
    guard let down = CGEvent(keyboardEventSource: nil, virtualKey: key, keyDown: true),
          let up = CGEvent(keyboardEventSource: nil, virtualKey: key, keyDown: false) else { exit(1) }
    down.postToPid(pid)
    Thread.sleep(forTimeInterval: hold)
    up.postToPid(pid)
}
