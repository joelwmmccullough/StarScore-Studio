#!/bin/bash
# StarScore Studio: finishes an update on macOS.
#
# The app writes this script to a temporary file and starts it detached just before it quits
# (AppUpdateScenario::startInstallerOnQuit). Arguments:
#   $1  the downloaded dmg (in the user's Downloads folder)
#   $2  the installed app bundle to replace, e.g. "/Applications/StarScore Studio.app"
#   $3  the pid of the quitting app; the script waits for it to exit before touching the bundle
#
# It is `set -u` but deliberately not `set -e`: every step that can fail is checked by hand and logged, so a
# failed copy leaves a clear line in ~/Library/Logs/StarScore Studio/update.log and the old app in place.
# When anything is off (the app is not in /Applications or ~/Applications, the dmg holds no single .app, the copy
# fails) the script falls back to opening the dmg in Finder, which is what MuseScore's updater did before.

set -u

DMG="${1:-}"
APP="${2:-}"
PID="${3:-}"

LOG_DIR="$HOME/Library/Logs/StarScore Studio"
mkdir -p "$LOG_DIR" 2>/dev/null || LOG_DIR="${TMPDIR:-/tmp}"
LOG="$LOG_DIR/update.log"

MOUNT_POINT=""

log() {
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"
}

# The script is a temp file; remove it once done, whatever happened.
cleanup_self() {
    rm -f -- "$0" 2>/dev/null
}
trap cleanup_self EXIT

detach_dmg() {
    if [ -n "$MOUNT_POINT" ] && [ -d "$MOUNT_POINT" ]; then
        if ! hdiutil detach "$MOUNT_POINT" >> "$LOG" 2>&1; then
            sleep 2
            hdiutil detach -force "$MOUNT_POINT" >> "$LOG" 2>&1 || log "could not detach $MOUNT_POINT"
        fi
    fi
    MOUNT_POINT=""
}

# Last resort: behave like the old updater and let the user drag the app out of the dmg themselves.
fallback_open_dmg() {
    detach_dmg
    log "falling back to opening the dmg in Finder: $DMG"
    if [ -f "$DMG" ]; then
        open "$DMG" >> "$LOG" 2>&1 || log "open failed for $DMG"
    fi
    exit 1
}

log "---- update started: dmg=\"$DMG\" app=\"$APP\" pid=\"$PID\""

if [ -z "$DMG" ] || [ ! -f "$DMG" ]; then
    log "dmg not found; nothing to install"
    exit 1
fi

if [ -z "$APP" ] || [ ! -d "$APP" ]; then
    log "installed app bundle not found"
    fallback_open_dmg
fi

# Only ever replace an app bundle in /Applications or ~/Applications. A build run from a build folder, a
# mounted dmg or anywhere else is left alone so nothing unexpected is overwritten.
case "$APP" in
    /Applications/*.app) ;;
    "$HOME"/Applications/*.app) ;;
    *)
        log "installed app is not in /Applications or ~/Applications; not replacing it"
        fallback_open_dmg
        ;;
esac

case "$APP" in
    *..*)
        log "installed app path contains '..'; not replacing it"
        fallback_open_dmg
        ;;
esac

# Wait for the app to quit (it dispatches the quit right after starting this script). Give up after two minutes:
# a save dialog the user left open, for example, means the quit did not happen.
if [ -n "$PID" ]; then
    waited=0
    while kill -0 "$PID" 2>/dev/null; do
        sleep 0.5
        waited=$((waited + 1))
        if [ "$waited" -ge 240 ]; then
            log "app (pid $PID) is still running after 120 s; not installing"
            exit 1
        fi
    done
fi
sleep 1

ATTACH_OUTPUT=$(hdiutil attach -nobrowse -readonly -noverify "$DMG" 2>> "$LOG")
if [ $? -ne 0 ] || [ -z "$ATTACH_OUTPUT" ]; then
    log "hdiutil attach failed for $DMG"
    fallback_open_dmg
fi

# hdiutil prints one line per entry ("/dev/disk4s1 <tab> Apple_HFS <tab> /Volumes/StarScore Studio"); the mount
# point is the last field of the /Volumes line and may contain spaces, so take everything from "/Volumes/" on.
MOUNT_POINT=$(printf '%s\n' "$ATTACH_OUTPUT" | grep -o '/Volumes/.*' | tail -n 1 \
    | sed -e 's/[[:space:]]*$//')
if [ -z "$MOUNT_POINT" ] || [ ! -d "$MOUNT_POINT" ]; then
    log "could not find the mount point in hdiutil output:"
    printf '%s\n' "$ATTACH_OUTPUT" >> "$LOG"
    MOUNT_POINT=""
    fallback_open_dmg
fi
log "mounted at $MOUNT_POINT"

NEW_APP=""
app_count=0
for candidate in "$MOUNT_POINT"/*.app; do
    [ -d "$candidate" ] || continue
    NEW_APP="$candidate"
    app_count=$((app_count + 1))
done
if [ "$app_count" -ne 1 ] || [ -z "$NEW_APP" ]; then
    log "expected exactly one .app in $MOUNT_POINT, found $app_count"
    fallback_open_dmg
fi
log "new app: $NEW_APP"

# Copy next to the installed app first and swap afterwards, so a failed copy (disk full, no permission) leaves
# the old app untouched instead of leaving no app at all.
PARENT=$(dirname "$APP")
STAGING="$PARENT/.StarScore Studio update $$.app"
OLD_APP="$PARENT/.StarScore Studio old $$.app"

rm -rf "$STAGING"
if ! ditto "$NEW_APP" "$STAGING" >> "$LOG" 2>&1; then
    log "ditto failed copying $NEW_APP to $STAGING"
    rm -rf "$STAGING"
    fallback_open_dmg
fi

if ! mv "$APP" "$OLD_APP" >> "$LOG" 2>&1; then
    log "could not move the old app aside ($APP -> $OLD_APP)"
    rm -rf "$STAGING"
    fallback_open_dmg
fi

if ! mv "$STAGING" "$APP" >> "$LOG" 2>&1; then
    log "could not move the new app into place; restoring the old one"
    mv "$OLD_APP" "$APP" >> "$LOG" 2>&1 || log "could not restore $OLD_APP to $APP"
    rm -rf "$STAGING"
    fallback_open_dmg
fi

# This is the app's own previous bundle, not user data.
rm -rf "$OLD_APP" || log "could not remove the old bundle $OLD_APP"

detach_dmg

# The dmg was downloaded, so the copied app carries the quarantine flag and an unsigned build would be refused
# by Gatekeeper. Harmless when the attribute is absent (xattr then just complains).
xattr -dr com.apple.quarantine "$APP" >> "$LOG" 2>&1 || true

log "installed $APP from $DMG"

if ! open "$APP" >> "$LOG" 2>&1; then
    log "open failed for $APP"
    exit 1
fi

exit 0
