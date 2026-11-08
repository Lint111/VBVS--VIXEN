#!/usr/bin/env bash
# Private, authenticated X11 display for scripted windowed witnesses (T-2693, T-4303).
#
# WSLg mounts /tmp/.X11-unix read-only, so an Xvfb cannot bind a unix socket. This listens on TCP
# only (cookie-authenticated, display DISPLAY=127.0.0.1:N) and keeps every artifact under <state-dir>:
# Xvfb and its libraries come from `apt-get download` + `dpkg-deb -x` (no root, no global change);
# xkbcomp is overlaid onto /usr/bin inside a bubblewrap mount namespace because Xvfb hard-codes that path.
#
#   private-x11-display.sh start <state-dir>   prints `export DISPLAY=... XAUTHORITY=...` lines
#   private-x11-display.sh stop  <state-dir>   bounded TERM, then KILL, of the recorded PIDs only
set -euo pipefail

packages=(xvfb libunwind8 libxfont2 libfontenc1 x11-xkb-utils xkb-data xserver-common libxkbfile1)
first_display=90
last_display=199

usage() { echo "Usage: $0 start|stop <state-dir>" >&2; exit 2; }
[[ $# -eq 2 ]] || usage
action="$1"
mkdir -p -- "$2"
state="$(cd -- "$2" && pwd -P)"
root="$state/root"
pid_file="$state/pids"

provision() {
    [[ -x "$root/usr/bin/Xvfb" ]] && return
    command -v bwrap >/dev/null || { echo "bubblewrap (bwrap) is required" >&2; exit 2; }
    command -v xauth >/dev/null || { echo "xauth is required" >&2; exit 2; }
    mkdir -p "$state/debs" "$root" "$state/xkbbin"
    (cd "$state/debs" && apt-get download "${packages[@]}" </dev/null >"$state/apt.log" 2>&1) \
        || { echo "apt-get download failed; see $state/apt.log" >&2; exit 2; }
    for deb in "$state"/debs/*.deb; do dpkg-deb -x "$deb" "$root"; done
    cp -f "$root/usr/bin/xkbcomp" "$state/xkbbin/xkbcomp"
}

start() {
    provision
    [[ -s "$pid_file" ]] && { echo "already started: $pid_file (stop it first)" >&2; exit 2; }
    export LD_LIBRARY_PATH="$root/usr/lib/x86_64-linux-gnu"
    local cookie auth display number
    cookie="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
    auth="$state/xauthority"
    for ((number = first_display; number <= last_display; ++number)); do
        ss -ltn "sport = :$((6000 + number))" | grep -q LISTEN && continue
        display="127.0.0.1:$number"
        rm -f -- "$auth"
        xauth -f "$auth" add "$display" MIT-MAGIC-COOKIE-1 "$cookie" 2>/dev/null
        bwrap --bind / / --dev-bind /dev /dev --overlay-src /usr/bin --overlay-src "$state/xkbbin" \
            --ro-overlay /usr/bin "$root/usr/bin/Xvfb" ":$number" -screen 0 1280x720x24 \
            -nolisten unix -listen tcp -auth "$auth" -xkbdir "$root/usr/share/X11/xkb" -fp built-ins \
            </dev/null >"$state/xvfb.log" 2>&1 &
        local bwrap_pid=$!
        for _ in $(seq 20); do
            ss -ltn "sport = :$((6000 + number))" | grep -q LISTEN && break
            kill -0 "$bwrap_pid" 2>/dev/null || break
            sleep 0.25
        done
        if kill -0 "$bwrap_pid" 2>/dev/null && ss -ltn "sport = :$((6000 + number))" | grep -q LISTEN; then
            { echo "$bwrap_pid"; pgrep -P "$bwrap_pid" || true; } >"$pid_file"
            echo "export DISPLAY=$display XAUTHORITY=$auth"
            return
        fi
        wait "$bwrap_pid" 2>/dev/null || true
    done
    echo "no private display could start; see $state/xvfb.log" >&2
    exit 1
}

stop() {
    [[ -s "$pid_file" ]] || return 0
    local pids pid
    mapfile -t pids <"$pid_file"
    for pid in "${pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    for _ in $(seq 20); do
        local alive=0
        for pid in "${pids[@]}"; do kill -0 "$pid" 2>/dev/null && alive=1; done
        ((alive)) || break
        sleep 0.25
    done
    for pid in "${pids[@]}"; do kill -KILL "$pid" 2>/dev/null || true; done
    rm -f -- "$pid_file"
}

case "$action" in
    start) start ;;
    stop) stop ;;
    *) usage ;;
esac
