#pragma once
// The generic view contract: a consumer supplies one of these to UIRenderNode. The renderer
// hosts it without knowing any field/struct name — the consumer registers its own data model
// (typically by calling its schema-generated Bind<Name>Model) and owns its storage. Renderer-
// agnostic view contract, Inc-2 (see Renderer-Agnostic-View-Contract-Design-2026-07.md).
#include <cstdint>

namespace Rml { class DataModelConstructor; }

namespace Vixen::RenderGraph {

class IView {
public:
    virtual ~IView() = default;
    /// The RmlUi data-model name (must match the document's data-model="…"). e.g. "hud".
    virtual const char* ModelName() const = 0;
    /// Register scalars/structs/arrays on the constructor and Bind() them to the consumer's own
    /// storage. Called once, after CreateDataModel, before LoadDocument.
    virtual void Register(Rml::DataModelConstructor& c) = 0;
    /// The RML document to load (relative "assets/ui/…" path, resolved by the node).
    virtual const char* DocumentPath() const = 0;
};

/// Minimal multi-document composition host (relational-vertical-slice M-ui; the M3 spike proved the
/// mechanics — see 2026-07-19-ui-composition-m3-spike.md §6). Mounts an IView as a SECOND RmlUi
/// document beside the primary (HUD) document, in the same shared Rml::Context, in the view's own
/// isolated data-model namespace. The single-document restriction was purely UIRenderNode's C++ shape
/// (one document_/viewModel_); this lifts it to N without any new rendering machinery.
///
/// Lifecycle is transactional (spike §5): Mount/Unmount are synchronous CPU ops on the context inside
/// the per-frame path. No focus machinery (nothing holds focus yet). Mount VALIDATES up front — RmlUi
/// silently renders nothing on a namespace collision or a degenerate 0×0 layout (spike §4), so the
/// host catches these rather than shipping a blank fragment.
class IUiCompositionHost {
public:
    // Opaque mount handle; 0 == invalid (a failed Mount returns 0).
    using MountHandle = uint32_t;

    virtual ~IUiCompositionHost() = default;

    /// Mount `view` as a second document. Returns a non-zero handle on success, or 0 if the view's
    /// ModelName() collides with an already-mounted model (or the primary "hud" model), the document
    /// fails to load, or the loaded document has a degenerate (0-size) layout. `view` must outlive
    /// the mount (the host stores a borrowed pointer; the caller owns it, as WireHudView does today).
    virtual MountHandle Mount(IView& view) = 0;

    /// Unmount a previously-mounted fragment. Tears down its document + isolated data model. A no-op
    /// for handle 0 or an already-unmounted handle.
    virtual void Unmount(MountHandle handle) = 0;

    /// Dirty a bound variable on a mounted fragment's data model (forwards to its DataModelHandle),
    /// so a pushed value re-renders. No-op if the handle isn't mounted.
    virtual void MarkMountedDirty(MountHandle handle, const char* field) = 0;

    /// True iff `handle` names a live mounted fragment with an alive document (the spike's
    /// hudDocAlive-style liveness check, for the M-ui gate + a mount/unmount test).
    virtual bool IsMounted(MountHandle handle) const = 0;
};

}  // namespace Vixen::RenderGraph
