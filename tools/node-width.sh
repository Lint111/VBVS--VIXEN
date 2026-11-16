#!/usr/bin/env bash
# Heavy payloads run at node width, capped by memory (owner ruling R847.4). Source this file.
#
#   jobs = max(1, min(cores, floor((available_mb - headroom_mb) / per_job_mb)))
#
# The queue grant owns a queued payload's width. This helper retains the generic node-width rule for
# direct WSL tools; native queued builds read grant-cpus from UT_QUEUE_GRANT_FILE and use the queue's
# jobserver for live top-ups. No shell-side claim files participate in either decision.
#
#   nw_logical_cores                              cores this process may run on
#   nw_wsl_available_mb                           WSL MemAvailable in MB
#   nw_queue_grant_cpus                           prints the current queue grant-cpus, when available
#   nw_manual_grant_cpus                          prints a manual queue grant-cpus, when available
#   nw_jobs CORES AVAILABLE_MB PER_JOB_MB [HEADROOM_MB]   the direct node-width rule, prints the worker count
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

nw_queue_grant_cpus() {
    local file="${UT_QUEUE_GRANT_FILE:-}" row field cpus=""
    [[ -n "$file" && -r "$file" ]] || return 1
    IFS= read -r row <"$file" 2>/dev/null || [[ -n "$row" ]] || return 1
    for field in $row; do
        case "$field" in grant-cpus=*) cpus="${field#grant-cpus=}" ;; esac
    done
    [[ "$cpus" =~ ^[1-9][0-9]*$ ]] || return 1
    printf '%s' "$cpus"
}

nw_jobs() {
    local cores="$1" available="$2" per_job="$3" headroom="${4:-$NW_HEADROOM_MB_DEFAULT}" by_memory
    [[ "$cores" =~ ^[1-9][0-9]*$ && "$per_job" =~ ^[1-9][0-9]*$ && "$available" =~ ^[0-9]+$ ]] || return 1
    by_memory=$(( (available - headroom) / per_job ))
    (( available <= headroom )) && by_memory=0
    (( by_memory < 1 )) && by_memory=1
    (( by_memory < cores )) && echo "$by_memory" || echo "$cores"
}
