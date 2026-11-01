#!/bin/sh
# Drive the user's logged-in Safari from the shell (osascript), for sites where they already have a session.
#   safari_tab.sh list                 titles and URLs of every tab
#   safari_tab.sh open URL [SECONDS]   open URL in a new tab of the front window, wait (default 4)
#   safari_tab.sh js 'JAVASCRIPT'      run it in the front window's current tab and print the result
# Needs Safari > Develop > Allow JavaScript from Apple Events (already on here).
set -eu
case "${1:-}" in
list)
    osascript -e 'tell application "Safari" to get {name, URL} of every tab of every window' ;;
open)
    osascript -e "tell application \"Safari\"
 tell front window to set current tab to (make new tab with properties {URL:\"$2\"})
 delay ${3:-4}
end tell" ;;
js)
    osascript -e 'on run argv
 tell application "Safari" to do JavaScript (item 1 of argv) in current tab of front window
end run' "$2" ;;
*)
    sed -n '2,6p' "$0"; exit 2 ;;
esac
