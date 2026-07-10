# FetchContent Dep-Cache Auto-Heal — Design Note (2026-07-10)

**Status:** DESIGN (not yet implemented). Small build-infra task, separate from the #2 view-framework work.
**Motivates:** KI-021 (existing build dirs keep the stale pre-v0.9.2 Gaia after a pin bump because
FetchContent caches `_deps` and never re-fetches on a `GIT_TAG` change). Turn that silent-stale
footgun into a configure-time self-heal, and add a deliberate local-override path + a clear knob.

## Goal
A reusable CMake helper (NOT Gaia-specific — any FetchContent dep can opt in) that, at configure time:
- **(A) Reconciles the cache against the pin** — default, always on. Self-heals the drift.
- **(B) Optionally adopts a newer LOCAL checkout** within a version ceiling — opt-in, loud, DAG-bounded.
- Exposes a **cache-clear param** to force a re-fetch.

User decisions (2026-07-10): do BOTH A+B (user chose A+B over A-only, accepting the reproducibility
tradeoff for local-dev flexibility); the B ceiling is **git ancestor-of-ceiling-tag** reachability
(NOT semver `--describe`, which is fuzzy for between-tags commits).

## Behaviour

### A — reconcile against pin (default, unconditional)
At configure, for a managed dep with cache dir `<builddir>/_deps/<dep>-src`:
```
if EXISTS <dep>-src/.git:
    cached_sha = git -C <dep>-src rev-parse HEAD
    if cached_sha != <resolved pinned commit>:   # pin is a SHA; exact match, not semver <=
        message(STATUS "<dep>: cached <cached_sha> != pin <pin_sha> — clearing _deps to re-fetch")
        rm -rf <builddir>/_deps/<dep>-src <dep>-build <dep>-subbuild
        # next FetchContent_MakeAvailable re-fetches at the pin
```
- Comparison is **exact commit identity** (cache == pinned commit), because the pin is a git SHA, not
  a semver — "under or equal" numeric comparison does not apply to a SHA. If it isn't exactly the
  pinned commit, it's wrong → clear. This keeps the pinned SHA the single reproducible source of truth.
- Pure-A configure (B flag off) is fully deterministic: CI and fresh clones always build the pin.

### B — discover + adopt a newer local checkout (opt-in: `-DVIXEN_DEP_ADOPT_LOCAL=ON`)
Only when the flag is ON. Lets a local dev build against an unreleased-but-local newer Gaia without
editing the pin. Gated so it can NEVER silently poison a build:
```
requires: -DVIXEN_GAIA_MAX_TAG=<ceiling tag>   (e.g. v0.9.9)  — the upper bound
scan candidate local checkouts L (configured search paths + any sibling gaia-ecs clones)
for each L with a .git:
    L_sha = git -C L rev-parse HEAD
    # L must be strictly NEWER than the pin AND <= the ceiling, on the real git DAG:
    git -C L merge-base --is-ancestor <pin_sha> L_sha       => pin is ancestor of L (L newer)
    git -C L merge-base --is-ancestor L_sha <ceiling_tag>   => L is ancestor-or-equal of ceiling
    if BOTH:
        message(WARNING "<dep>: ADOPTING LOCAL checkout ${L} @ ${L_sha} — newer than pin "
                        "${pin_sha}, within ceiling ${ceiling_tag}. BUILD IS NOT REPRODUCIBLE "
                        "against the pin. Set VIXEN_DEP_ADOPT_LOCAL=OFF for a pinned build.")
        point the dep at L (FetchContent_Declare SOURCE_DIR ${L}, or override FETCHCONTENT_SOURCE_DIR_<DEP>)
        break
```
- **Loud by construction:** every adopted-local build prints the WARNING — a non-pin build always
  announces itself. No silent divergence.
- **DAG-bounded:** the ceiling tag's ref must be fetched to run the reachability check (a lightweight
  `git fetch <remote> tag <ceiling>` into the candidate, or resolve against the already-cloned L). If
  the ceiling tag can't be resolved, B is skipped with a warning (fail toward the pin, never adopt
  unbounded).
- Ancestor checks (not semver) chosen deliberately: a between-tags commit `describe`s as
  `vX.Y.Z-N-gSHA` which is ambiguous to parse; `merge-base --is-ancestor` is exact on the commit DAG.

### Cache-clear param: `-DVIXEN_CLEAR_DEP_CACHE=ON`
Force-wipe `_deps/<dep>-*` for the managed dep(s) at configure so the next `MakeAvailable` re-fetches,
without hand-`rm`. Independent of A/B (A already clears on mismatch; this is the explicit override for
"re-fetch even if it matches", e.g. a corrupt cache).

## Shape / packaging
- A function `vixen_managed_fetchcontent(<dep> GIT_REPOSITORY … GIT_TAG <sha> [CEILING_TAG_VAR …])`
  wrapping `FetchContent_Declare`/`MakeAvailable` with the A/B/clear logic, in a new
  `cmake/ManagedFetchContent.cmake` module. Gaia is the first adopter; the existing gaia block in
  `VIXEN/dependencies/CMakeLists.txt` migrates to it.
- Options: `VIXEN_DEP_ADOPT_LOCAL` (OFF default), `VIXEN_CLEAR_DEP_CACHE` (OFF), `VIXEN_GAIA_MAX_TAG`
  (unset default; required only when adopt-local is ON for gaia). Generalizable per-dep later.
- `rm -rf` at configure runs via `file(REMOVE_RECURSE …)` (cross-platform, no shell) — NOT a raw shell
  `rm`, so it works on Windows configures too.

## Verification plan (when built)
1. **A self-heal:** with a build dir whose `_deps/gaia-src` is at the stale `6f0a947`, reconfigure →
   configure clears + re-fetches → `git -C _deps/gaia-src rev-parse HEAD` == `f2ea77a` (v0.9.2). This
   is exactly the KI-021 repro.
2. **A no-op when matched:** cache already at pin → no clear, no re-fetch, fast reconfigure.
3. **B adopt:** with `-DVIXEN_DEP_ADOPT_LOCAL=ON -DVIXEN_GAIA_MAX_TAG=<tag>` and a local newer
   in-ceiling checkout → WARNING printed, build uses L; with a local checkout ABOVE the ceiling →
   NOT adopted (stays on pin). With the flag OFF → L ignored entirely (pure A).
4. **Clear param:** `-DVIXEN_CLEAR_DEP_CACHE=ON` wipes + re-fetches even on a matching cache.
5. **No-regression:** a normal configure+build (flag off) is byte-identical in behaviour to today's
   pinned fetch; Gaia libs + tests still green.
6. Closes KI-021 (mark RESOLVED with the implementing commit).

## Scope / sequencing
Independent of the #2 view-framework design. Can run as its own small [[post-brainstorm-context-manager]]
milestone (one implementer + Opus validator) whenever convenient — does NOT block #2. Do not overlap
its configure with other work on the shared checkout (own worktree).
