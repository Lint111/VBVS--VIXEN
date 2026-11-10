#!/usr/bin/env bash
# Heavy payloads run at node width, capped by memory (owner ruling R847.4). Source this file.
#
#   jobs = max(1, min(cores, floor((available_mb - headroom_mb) / per_job_mb)))
#
# The queue grant sets admission, priority and cpu.weight; it never sets a payload's parallelism, and an
# explicit override (the caller's own variable or flag) wins. available_mb is measured when the step
# starts, minus the memory other queue holders have reserved or claimed and not yet used
# (max(reserved, claim) - current per other live holder). The reservations are the queue's
# undertow-box-memory-reservation.<pid> files; the claims are undertow-width-claim.<pid> files, which are
# the same files undertow's tools/native_boot_jobs.py writes and reads, so a VIXEN build and an undertow
# boot starting back to back see each other. Memory already used is part of the measured available figure
# and is not counted twice.
#
#   nw_logical_cores                              cores this process may run on
#   nw_wsl_available_mb                           WSL MemAvailable in MB
#   nw_other_uncommitted_mb                       prints "<mb> <holders>"; skips this holder
#   nw_jobs CORES AVAILABLE_MB PER_JOB_MB [HEADROOM_MB]   the rule, prints the worker count
#   nw_claim_begin / nw_claim_end MB LABEL        serialise measure+claim; publish the claim; nw_claim_release drops it
# Fake inputs for tests: NW_FAKE_CORES, NW_FAKE_AVAILABLE_MB.

NW_HEADROOM_MB_DEFAULT=1024

nw_state_dir() { printf '%s' "${UT_QUEUE_STATE_DIR:-/tmp}"; }

nw_logical_cores() {
    if [[ -n "${NW_FAKE_CORES:-}" ]]; then echo "$NW_FAKE_CORES"; return 0; fi
    local n
    n="$(nproc 2>/dev/null || true)"
    echo "${n:-1}"
}

nw_wsl_available_mb() {
    if [[ -n "${NW_FAKE_AVAILABLE_MB:-}" ]]; then echo "$NW_FAKE_AVAILABLE_MB"; return 0; fi
    awk '/^MemAvailable:/ { print int($2 / 1024); exit }' /proc/meminfo 2>/dev/null
}

nw_start_ticks() { sed 's/.*) //' "/proc/$1/stat" 2>/dev/null | awk '{ print $20 }'; }

nw_other_uncommitted_mb() {
    local dir file pid ticks recorded committed current total=0 holders=0 own="${UT_QUEUE_HOLDER_PID:-}" seen=" "
    local -A reserved=() claimed=() current_of=() start_of=()
    dir="$(nw_state_dir)"
    for file in "$dir"/undertow-box-memory-reservation.[0-9]* "$dir"/undertow-width-claim.[0-9]*; do
        [[ -f "$file" ]] || continue
        pid="${file##*.}"
        [[ "$pid" =~ ^[0-9]+$ && "$pid" != "$own" ]] || continue
        committed="$(sed -n '1p' "$file" 2>/dev/null || true)"
        [[ "$committed" =~ ^[0-9]+$ ]] || continue
        recorded="$(sed -n 's/^start_ticks=//p' "$file" 2>/dev/null | head -1 || true)"
        ticks="$(nw_start_ticks "$pid")"
        [[ -n "$ticks" ]] || continue
        [[ -z "$recorded" || "$recorded" == "$ticks" ]] || continue
        if [[ "$file" == *undertow-box-memory-reservation.* ]]; then
            reserved[$pid]="$committed"
            current="$(sed -n 's/^current=//p' "$file" 2>/dev/null | head -1 || true)"
            [[ "$current" =~ ^[0-9]+$ ]] && current_of[$pid]="$current"
        else
            claimed[$pid]="$committed"
        fi
    done
    for pid in "${!reserved[@]}" "${!claimed[@]}"; do
        [[ "$seen" == *" $pid "* ]] && continue
        seen+="$pid "
        committed="${reserved[$pid]:-0}"
        (( ${claimed[$pid]:-0} > committed )) && committed="${claimed[$pid]}"
        current="${current_of[$pid]:-0}"
        (( committed > current )) && total=$((total + committed - current))
        holders=$((holders + 1))
    done
    # The queue will export other jobs' unspent reservations itself; never size below that figure.
    [[ "${UT_QUEUE_RESERVED_OTHERS_MB:-}" =~ ^[0-9]+$ ]] && (( UT_QUEUE_RESERVED_OTHERS_MB > total )) && total="$UT_QUEUE_RESERVED_OTHERS_MB"
    echo "$total $holders"
}

nw_jobs() {
    local cores="$1" available="$2" per_job="$3" headroom="${4:-$NW_HEADROOM_MB_DEFAULT}" by_memory
    [[ "$cores" =~ ^[1-9][0-9]*$ && "$per_job" =~ ^[1-9][0-9]*$ && "$available" =~ ^[0-9]+$ ]] || return 1
    by_memory=$(( (available - headroom) / per_job ))
    (( available <= headroom )) && by_memory=0
    (( by_memory < 1 )) && by_memory=1
    (( by_memory < cores )) && echo "$by_memory" || echo "$cores"
}

# Hold the node-wide claim lock from measuring to publishing the claim, so two heavy steps starting at
# the same moment cannot both read the same free memory. fd 9 is held until nw_claim_end.
nw_claim_begin() {
    [[ -n "${UT_QUEUE_HOLDER_PID:-}" ]] || return 0
    exec 9>>"$(nw_state_dir)/undertow-width-claim.lock" 2>/dev/null || return 0
    flock 9 2>/dev/null || true
}

nw_claim_end() {
    local mb="$1" label="$2" dir tmp
    if [[ -n "${UT_QUEUE_HOLDER_PID:-}" ]]; then
        dir="$(nw_state_dir)"
        tmp="$(mktemp "$dir/.undertow-width-claim.XXXXXX" 2>/dev/null || true)"
        if [[ -n "$tmp" ]]; then
            printf '%s\npid=%s\nstart_ticks=%s\nlabel=%s\n' "$mb" "$UT_QUEUE_HOLDER_PID" "$(nw_start_ticks "$UT_QUEUE_HOLDER_PID")" "$label" > "$tmp" \
                && mv -f -- "$tmp" "$dir/undertow-width-claim.$UT_QUEUE_HOLDER_PID" || rm -f -- "$tmp"
        fi
    fi
    exec 9>&- 2>/dev/null || true
}

nw_claim_release() {
    [[ -n "${UT_QUEUE_HOLDER_PID:-}" ]] && rm -f -- "$(nw_state_dir)/undertow-width-claim.$UT_QUEUE_HOLDER_PID"
    return 0
}
