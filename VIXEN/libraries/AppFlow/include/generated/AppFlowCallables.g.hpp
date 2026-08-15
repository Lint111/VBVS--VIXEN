#pragma once
// HAND-AUTHORED (row-deltas link 2/step-7B) -- NOT tool-generated, NOT drift-checked. Every
// UndertowHud*.typed.g.h #includes this well-known path; the tool-owned --callable-cpp output
// now lives at AppFlowCallables.generated.g.hpp (see codegen/CMakeLists.txt's callables_regen
// target comment) because one [KernelCallable] the schema documents --
// UndertowViewCallables.IdentityString(string v) -- cannot be transpiled: --callable-cpp's
// CppMappingTables (Yeroket kernel-framework, out of scope for this repo to edit) has no
// string->C++ mapping, so a transpiled `inline string IdentityString(string v)` is not valid C++
// (no bare "string" type exists in C++). This shim supplies the one missing symbol by hand --
// same "consumer defines by hand" precedent as EditorLayersView's
// BindEditorLayersModel_activeLayerCountOverride hook (VIXEN/application/editor) -- and forwards
// everything else to the real generated header unchanged.
// The generated header's own params (e.g. StrengthBandToByte's rowIndex) are unused by design --
// tool output must match a fixed callable signature even when a specific body ignores an arg.
// Scoped to just this one #include, not the whole TU -- consumer code stays fully checked.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "generated/AppFlowCallables.generated.g.hpp"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <RmlUi/Core/Types.h>   // Rml::String

namespace Vixen::AppFlow::Generated {

// UndertowHudInspect.typed.g.h's cause() accessor: a pure name-binding identity projection
// (view field `cause` <- C# source member `el.CauseString`, no value transform) -- see
// UndertowViewCallables.cs's IdentityString for the documented Source-traceability pointer.
inline Rml::String IdentityString(const Rml::String& v) {
    return v;
}

} // namespace Vixen::AppFlow::Generated
