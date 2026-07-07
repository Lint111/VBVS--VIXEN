#pragma once
#include "Ui/ViewBlob.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <deque>

namespace Vixen::RenderGraph {

// Runtime .viewblob parser (Inc-2b data-file delivery front-end). Owns the backing storage the
// produced ViewBlob's string_views/spans point into — keep the ViewBlobFile alive while using Blob().
// All malformed/missing input returns nullopt (logged), never throws.
class ViewBlobFile {
public:
    static std::optional<ViewBlobFile> Parse(std::string_view text);
    static std::optional<ViewBlobFile> Load(const std::string& path);
    const ViewBlob& Blob() const { return blob_; }

    ViewBlobFile(ViewBlobFile&&) = default;             // moves keep storage stable (deque/list)
    ViewBlobFile& operator=(ViewBlobFile&&) = default;
    ViewBlobFile(const ViewBlobFile&) = delete;
private:
    ViewBlobFile() = default;
    // Stable-address backing storage: deque never invalidates element addresses on growth, so the
    // spans/string_views into these stay valid.
    std::deque<std::string>        strings_;            // owns every name string
    std::deque<std::vector<ViewFieldDesc>> elemArrays_; // owns each elem[] array
    std::vector<ViewFieldDesc>     topFields_;
    ViewBlob                       blob_{};
};

}  // namespace Vixen::RenderGraph
