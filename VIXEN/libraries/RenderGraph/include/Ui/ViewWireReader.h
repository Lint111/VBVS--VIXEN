#pragma once
#include "Ui/ViewStore.h"
#include <cstddef>
#include <span>

namespace Vixen::RenderGraph {

// Reads a UTVA AoS wire buffer (View Contract Inc-3, spec §4) into a ViewStore, guided by the
// store's ViewBlob (declared field order + kinds). Version-checked at entry against store.Version().
// Returns false + logs (LT_ERROR) on any version mismatch or malformed input; never throws, never
// over-reads (bounds-checked against the span).
//
// Failure contract (spec §5.3/§8, mirrors ViewBlobFile::Parse): a version-mismatch, bad-magic, or
// field-count failure returns false BEFORE any field is written — store untouched. A malformed BODY
// (mid-walk overrun / trailing bytes) may leave some already-decoded fields written before returning
// false. So `false` means "do not consume/flush this result", NOT "store is pristine" — the caller
// must discard the store's contents on a false return rather than dirtying the model from it.
class ViewWireReader {
public:
    static bool Apply(std::span<const std::byte> wire, ViewStore& store);
};

}  // namespace Vixen::RenderGraph
