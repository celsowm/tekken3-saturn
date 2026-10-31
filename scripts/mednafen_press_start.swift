// Send a short Return-key pulse to one Mednafen process on macOS.
import ApplicationServices
import Foundation

guard CommandLine.arguments.count == 4,
      let pid = Int32(CommandLine.arguments[1]),
      let delay = Double(CommandLine.arguments[2]),
      let hold = Double(CommandLine.arguments[3]),
      delay >= 0, hold > 0, AXIsProcessTrusted() else {
    fputs("Usage: mednafen_press_start.swift PID DELAY_SECONDS HOLD_SECONDS (requires accessibility access)\n", stderr)
    exit(1)
}
Thread.sleep(forTimeInterval: delay)
guard let down = CGEvent(keyboardEventSource: nil, virtualKey: 36, keyDown: true),
      let up = CGEvent(keyboardEventSource: nil, virtualKey: 36, keyDown: false) else {
    fputs("Could not create keyboard events\n", stderr)
    exit(1)
}
down.postToPid(pid)
Thread.sleep(forTimeInterval: hold)
up.postToPid(pid)
