#!/bin/sh
# Answer the file dialog a page opened in Safari. Keystrokes go to whichever app is in front, so this types only after
# seeing Safari in front with the dialog open, and gives up otherwise.
#   safari_pick_file.sh state       print the front app and Safari's windows and sheets
#   safari_pick_file.sh PATH        choose PATH in the open dialog
set -eu
state='tell application "System Events"
 set frontApp to name of first process whose frontmost is true
 tell process "Safari" to return frontApp & " windows=" & (count of windows) & " sheets=" & (count of sheets of window 1)
end tell'
case "${1:-}" in
"") sed -n '2,6p' "$0"; exit 2 ;;
state) osascript -e 'tell application "Safari" to activate' -e 'delay 1' -e "$state"; exit 0 ;;
esac
osascript -e 'on run argv
 tell application "Safari" to activate
 delay 1
 tell application "System Events"
  if (name of first process whose frontmost is true) is not "Safari" then error "Safari is not in front"
  tell process "Safari"
   if (count of sheets of window 1) is 0 then error "no file dialog open"
   keystroke "g" using {command down, shift down}
   delay 1
   keystroke (item 1 of argv)
   delay 1
   keystroke return
   delay 1
   keystroke return
  end tell
 end tell
end run' "$1"
