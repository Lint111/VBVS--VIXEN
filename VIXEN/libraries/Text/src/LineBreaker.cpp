#include "Text/LineBreaker.h"

#include <linebreak.h>
#include <unibreakbase.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace Vixen::Text {
namespace {

std::size_t Utf8UnitLength(unsigned char lead) {
    if ((lead & 0x80U) == 0U)
        return 1;
    if ((lead & 0xE0U) == 0xC0U)
        return 2;
    if ((lead & 0xF0U) == 0xE0U)
        return 3;
    if ((lead & 0xF8U) == 0xF0U)
        return 4;
    return 1;
}

} // namespace

std::vector<LineBreakOpportunity> FindLineBreaks(const PresetDesc& preset,
                                                 std::string_view utf8Text) {
    std::vector<LineBreakOpportunity> result;
    if (utf8Text.empty() || utf8Text.size() > std::numeric_limits<std::uint32_t>::max())
        return result;

    // The class is carried by the preset boundary. libunibreak supplies the
    // common UAX #14 opportunities for each class; locale-specific dictionary
    // tailoring remains data owned by the preset producer.
    switch (preset.lineBreakingClass) {
    case LineBreakingClass::Word:
    case LineBreakingClass::Phrase:
    case LineBreakingClass::Dictionary:
        break;
    default:
        return result;
    }

    std::vector<char> breaks(utf8Text.size());
    const std::size_t codepointCount = set_linebreaks_utf8_per_code_point(
        reinterpret_cast<const utf8_t*>(utf8Text.data()), utf8Text.size(), nullptr, breaks.data());

    std::size_t byteOffset = 0;
    for (std::size_t codepointIndex = 0;
         codepointIndex < codepointCount && byteOffset < utf8Text.size(); ++codepointIndex) {
        const unsigned char lead = static_cast<unsigned char>(utf8Text[byteOffset]);
        byteOffset = std::min(byteOffset + Utf8UnitLength(lead), utf8Text.size());
        const auto opportunity = static_cast<unsigned char>(breaks[codepointIndex]);
        if (opportunity == LINEBREAK_ALLOWBREAK || opportunity == LINEBREAK_MUSTBREAK) {
            result.push_back({static_cast<std::uint32_t>(byteOffset),
                              opportunity == LINEBREAK_MUSTBREAK});
        }
    }
    return result;
}

} // namespace Vixen::Text
