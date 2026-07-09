#pragma once
// The generic view contract: a consumer supplies one of these to UIRenderNode. The renderer
// hosts it without knowing any field/struct name — the consumer registers its own data model
// (typically by calling its schema-generated Bind<Name>Model) and owns its storage. Renderer-
// agnostic view contract, Inc-2 (see Renderer-Agnostic-View-Contract-Design-2026-07.md).
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

}  // namespace Vixen::RenderGraph
