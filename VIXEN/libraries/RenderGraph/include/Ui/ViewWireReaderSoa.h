#pragma once
#include "Ui/ViewStore.h"
#include <cstddef>
#include <span>

namespace Vixen::RenderGraph {

// Reads a UTVA wire buffer whose ArrayOfStruct fields are SoA-encoded (View Contract Inc-5,
// per-column contiguous arrays sharing one row-count header; each String column carries its OWN
// (rowCount+1)-entry offsets array + a per-column blob) into a ViewStore, guided by the store's
// ViewBlob (declared field order + kinds). Sibling of ViewWireReader (Inc-3's AoS reader) rather
// than an extension of it: the ArrayOfStruct body's byte shape is genuinely different (columns,
// not rows), so branching mid-decode would obscure both paths. Scalar top-level fields use the
// identical encoding as AoS (only ArrayOfStruct bodies differ), so this reader still needs its
// own full walk since the two kinds of fields interleave in declared order.
//
// Same failure contract as ViewWireReader: version/magic/field-count mismatches return false
// before any field is written; a malformed BODY may leave already-decoded fields written before
// returning false — callers must discard the store's contents on a false return.
class ViewWireReaderSoa {
public:
    static bool Apply(std::span<const std::byte> wire, ViewStore& store);
};

}  // namespace Vixen::RenderGraph
