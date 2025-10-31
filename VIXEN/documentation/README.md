# VIXEN Documentation

**Last Updated**: October 31, 2025

## Quick Start

- **Project Status**: [`Project-Status-Summary.md`](Project-Status-Summary.md) - Current phase, milestones, metrics
- **Architecture**: [`RenderGraph-Architecture-Overview.md`](RenderGraph-Architecture-Overview.md) - System overview
- **Standards**: [`cpp-programming-guidelins.md`](cpp-programming-guidelins.md) - Code standards

## Documentation Structure

```
documentation/
├── README.md                              # This file
├── Project-Status-Summary.md              # Current state, milestones, metrics
├── RenderGraph-Architecture-Overview.md   # Main system architecture
├── ArchitecturalReview-2025-10.md         # Industry comparison (Unity, Unreal, Frostbite)
│
├── CashSystem/                            # Resource caching system
│   ├── Cleanup-Architecture.md            # Virtual cleanup pattern
│   ├── CashSystem-Integration.md          # Integration guide
│   ├── CashSystem-Migration-Status.md     # Migration tracking
│   ├── CashSystem-RenderGraph-Integration.md
│   └── CashSystem-Consumer-Analysis.md
│
├── ShaderManagement/                      # Shader compilation and reflection
│   ├── ShaderManagement-Integration-Plan.md  # 6-phase integration roadmap
│   ├── Shader-PreCompilation-Workflow.md     # GLSL→SPIR-V workflow
│   └── ShaderLibraryArchitecture.md          # Shader library design
│
├── EventBus/                              # Event system
│   ├── EventBusArchitecture.md            # Event-driven invalidation
│   └── EventBus-ResourceManagement-Integration.md
│
├── GraphArchitecture/                     # RenderGraph system (27 docs)
│   ├── README.md                          # Graph system overview
│   ├── 01-node-system.md                  # Node architecture
│   ├── 02-graph-compilation.md            # Compilation phases
│   ├── 03-multi-device.md                 # Multi-device support
│   ├── 04-caching.md                      # Caching strategies
│   ├── 05-implementation.md               # Implementation guide
│   ├── 06-examples.md                     # Example graphs
│   ├── NodeCatalog.md                     # All 15+ node types
│   ├── ResourceVariant-Quick-Reference.md # Resource type system
│   ├── TypedNodeExample.md                # Typed node usage
│   └── ... (20+ more detailed docs)
│
├── archive/                               # Outdated/superseded docs
│   ├── BugFixes-RenderGraphIntegration.md
│   ├── DynamicCleanupSystem.md            # Superseded by Cleanup-Architecture
│   ├── EventDrivenCleanup.md              # Superseded by EventBusArchitecture
│   └── ResourceVariant-Migration.md
│
└── Standards/
    ├── Communication Guidelines.md        # Claude Code interaction style
    ├── cpp-programming-guidelins.md       # C++ coding standards
    └── smart-pointers-guide.md            # Memory management guide
