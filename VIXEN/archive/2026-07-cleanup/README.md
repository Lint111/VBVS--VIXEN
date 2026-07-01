# Archive: 2026-07 Documentation Cleanup

Files moved here during the July 2026 documentation audit. Nothing is deleted — these are
finished/superseded artifacts pulled out of the repo root to reduce clutter. History is intact
via git; retrieve any file with `git mv` back if needed.

## Contents

### Resolved-incident post-mortems (Dec 2025 HacknPlan deletion incident)
- `DELETION-API-ENDPOINTS-CHECKED.md`
- `DELETION-RECOVERY-INVESTIGATION.md`
- `DELETION-RECOVERY-SUMMARY.txt`
- `README-DELETION-INVESTIGATION.txt`

Investigation concluded (permanent loss, no recovery mechanism). Kept for the audit trail only.

### Point-in-time planning/review docs (Jan 2026)
- `ARCHITECTURE_CRITIQUE_2026-01-03.md`
- `ARCHITECTURE_REVIEW_SUMMARY_2026-01-03.md`
- `ARCHITECTURE_QUICK_REFERENCE.md`
- `PRE_ALLOCATION_IMPLEMENTATION_GUIDE.md`

Superseded by the live architecture docs in `Vixen-Docs/01-Architecture/`. The pre-allocation
work these describe is complete; the current game-renderer direction is captured in
`Vixen-Docs/01-Architecture/Architecture-Review-Game-Renderer-2026-06-12.md`.

### Orphaned sample
- `test_simple_voxel_backend.cpp` — standalone voxel-workflow sample, not referenced by any
  `CMakeLists.txt`. Kept as an example.
