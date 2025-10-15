# Pull Request checklist

Please ensure the following before requesting review:

- [ ] I ran a Debug build locally: `cmake -B build; cmake --build build --config Debug` and verified the binary runs from `binaries/`.
- [ ] If I changed initialization or architecture, I updated `memory-bank/activeContext.md` and `memory-bank/progress.md` with a short summary.
- [ ] I documented any non-owning/raw pointer usage with `// non-owning` and left a short rationale.
- [ ] I did not add new hard-coded Vulkan SDK paths; if needed, updated `CMakeLists.txt` with flags `AUTO_LOCATE_VULKAN` or new variables.

Reviewer checklist

- [ ] Build passes on reviewer machine (or CI)
- [ ] Memory bank updated when appropriate
- [ ] No new manual `new`/`delete` without RAII wrapper

Notes:
- Small refactors are preferred in separate PRs (one subsystem per PR). This repository aims for incremental, reviewable changes.
