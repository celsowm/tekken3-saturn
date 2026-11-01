// Prints the window number of the first on-screen window owned by Mednafen, or nothing.
// For `screencapture -x -l $(swift scripts/mednafen_window_id.swift) out.png`: only the emulator's window is captured.
import CoreGraphics
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
for w in windows where (w[kCGWindowOwnerName as String] as? String)?.lowercased() == "mednafen" {
    if let id = w[kCGWindowNumber as String] as? Int { print(id); break }
}
