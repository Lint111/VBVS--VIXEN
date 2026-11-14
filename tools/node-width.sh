#!/usr/bin/env bash
# Heavy payloads run at node width, capped by memory (owner ruling R847.4). Source this file.
#
#   jobs = max(1, min(cores, floor((available_mb - headroom_mb) / per_job_mb)))
#
# The queue grant sets admission, priority and cpu.weight; it never sets a payload's parallelism, and an
# explicit override (the caller's own variable or flag) wins. available_mb is measured when the step
# starts. A WSL-side sizer (nw_other_uncommitted_mb) minus the memory other queue holders have reserved or claimed and not yet used
# (max(reserved, claim) - current per other live holder). The reservations are the queue's
# undertow-box-memory-reservation.<pid> files; the claims are undertow-width-claim.<pid> files, which are
# the same files undertow's tools/native_boot_jobs.py writes and reads, so a VIXEN build and an undertow
# boot starting back to back see each other. Memory already used is part of the measured available figure
# and is not counted twice.
#
#   nw_logical_cores                              cores this process may run on
#   nw_wsl_available_mb                           WSL MemAvailable in MB
#   nw_other_uncommitted_mb                       prints "<mb> <holders>"; skips this holder
#   nw_other_claims_mb                            Windows-side sizer: other builds' claims only (see its comment); nw_floor_jobs JOBS CORES AVAIL PER_JOB
#   nw_jobs CORES AVAILABLE_MB PER_JOB_MB [HEADROOM_MB]   the rule, prints the worker count
#   nw_claim_begin / nw_claim_end MB LABEL        serialise measure+claim; publish the claim; nw_claim_release drops it
# Fake inputs for tests: NW_FAKE_CORES, NW_FAKE_AVAILABLE_MB.

NW_HEADROOM_MB_DEFAULT=1024

# A manual queue grant (with-test-lock.sh --regrant, sn-qregrant1): the owner's explicit choice of width for this job. The grant file
# (UT_QUEUE_GRANT_FILE, the job's status row, rewritten atomically on every change) is read fresh on every call, so each build step sees
# the current value. Prints the CPUs and returns 0 only when the row says grant-manual=1; an automatic grant (R847.4: the payload sizes
# itself from the node), a missing file or an unreadable row print nothing and return 1.
nw_manual_grant_cpus() {
    local file="${UT_QUEUE_GRANT_FILE:-}" row field cpus="" manual=false
    [[ -n "$file" && -r "$file" ]] || return 1
    IFS= read -r row <"$file" 2>/dev/null || [[ -n "$row" ]] || return 1
    for field in $row; do
        case "$field" in
            grant-manual=1) manual=true ;;
            grant-cpus=*) cpus="${field#grant-cpus=}" ;;
        esac
    done
    [[ "$manual" == true && "$cpus" =~ ^[1-9][0-9]*$ ]] || return 1
    printf '%s' "$cpus"
}

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

# Width claims of other live holders, in MB, for a sizer whose "available" figure is the WINDOWS counter.
# Prints "<mb> <holders>". Reservations are deliberately not read: they are mostly WSL-side Linux
# admissions, and the Windows counter already nets the whole WSL VM (measured 11-11 on ws: Available
# MBytes 9553 while vmmemWSL held a 70508 MB working set, of which 63842 MB was still MemAvailable
# inside the VM). Memory a Linux job spends inside the VM comes out of that already-held pool and never
# touches the Windows counter, so subtracting its unspent reservation counts the same bytes twice.
# Another build's claim (jobs * per_job, published before its cl.exe start) is not in the counter only
# until its compilers ramp up, so it comes off the budget for NW_CLAIM_RAMP_S (120, INFERRED: ninja starts
# its full cl.exe set within seconds) after the claim file was written, and not after: a build running for
# minutes is already in the counter, and subtracting its whole claim again made the first witness run size
# 1 job off fb-lk3's 39528 MB claim (11-11, 33 min into its build).
NW_CLAIM_RAMP_S="${NW_CLAIM_RAMP_S:-120}"

nw_other_claims_mb() {
    local dir file pid ticks recorded claim age total=0 holders=0 own="${UT_QUEUE_HOLDER_PID:-}" now
    dir="$(nw_state_dir)"; now="$(date +%s)"
    for file in "$dir"/undertow-width-claim.[0-9]*; do
        [[ -f "$file" ]] || continue
        pid="${file##*.}"
        [[ "$pid" =~ ^[0-9]+$ && "$pid" != "$own" ]] || continue
        claim="$(sed -n '1p' "$file" 2>/dev/null || true)"
        [[ "$claim" =~ ^[0-9]+$ ]] || continue
        age=$(( now - $(stat -c %Y "$file" 2>/dev/null || echo "$now") ))
        (( age <= NW_CLAIM_RAMP_S )) || continue
        recorded="$(sed -n 's/^start_ticks=//p' "$file" 2>/dev/null | head -1 || true)"
        ticks="$(nw_start_ticks "$pid")"
        [[ -n "$ticks" ]] || continue
        [[ -z "$recorded" || "$recorded" == "$ticks" ]] || continue
        total=$((total + claim))
        holders=$((holders + 1))
    done
    echo "$total $holders"
}

# A build the queue admitted keeps at least this many jobs (capped at cores) unless the memory itself is
# below FLOOR * per_job. 4 is the width the per-job memory figure was measured at (cold -j4 build), so
# 4 * 900 MB = 3.6 GB is a footprint already known to fit; one job for 30 minutes (sn-landvx5, 11-10) is
# never the right answer for a build the queue judged worth a full-width slot.
NW_ADMITTED_FLOOR_JOBS=4

nw_floor_jobs() {
    local jobs="$1" cores="$2" available="$3" per_job="$4" floor="${NW_ADMITTED_FLOOR_JOBS}"
    (( floor > cores )) && floor="$cores"
    if (( jobs < floor && available >= floor * per_job )); then echo "$floor"; else echo "$jobs"; fi
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
