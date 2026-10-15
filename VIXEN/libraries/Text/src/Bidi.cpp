#include "Text/Bidi.h"

#include <SheenBidi/SBAlgorithm.h>
#include <SheenBidi/SBLine.h>
#include <SheenBidi/SBMirrorLocator.h>
#include <SheenBidi/SBParagraph.h>
#include <SheenBidi/SBRun.h>

#include <algorithm>
#include <limits>
#include <memory>

namespace Vixen::Text {

BidiResult ResolveBidi(const PresetDesc& preset, std::string_view utf8Text) {
    BidiResult result;
    if (utf8Text.empty() || utf8Text.size() > std::numeric_limits<std::uint32_t>::max() ||
        utf8Text.size() > std::numeric_limits<SBUInteger>::max())
        return result;

    const SBCodepointSequence sequence{
        SBStringEncodingUTF8,
        utf8Text.data(),
        static_cast<SBUInteger>(utf8Text.size())};
    using Algorithm = std::unique_ptr<const struct _SBAlgorithm, decltype(&SBAlgorithmRelease)>;
    Algorithm algorithm(SBAlgorithmCreate(&sequence), &SBAlgorithmRelease);
    if (!algorithm)
        return result;

    const SBLevel baseLevel = preset.direction == TextDirection::RightToLeft
                                  ? SBLevelDefaultRTL
                                  : SBLevelDefaultLTR;
    const SBUInteger totalLength = sequence.stringLength;
    SBUInteger paragraphOffset = 0;
    while (paragraphOffset < totalLength) {
        SBUInteger paragraphLength = 0;
        SBUInteger separatorLength = 0;
        SBAlgorithmGetParagraphBoundary(algorithm.get(), paragraphOffset,
                                        totalLength - paragraphOffset,
                                        &paragraphLength, &separatorLength);
        if (paragraphLength == 0 && separatorLength == 0)
            break;

        if (paragraphLength > 0) {
            using Paragraph = std::unique_ptr<const struct _SBParagraph, decltype(&SBParagraphRelease)>;
            Paragraph paragraph(SBAlgorithmCreateParagraph(algorithm.get(), paragraphOffset,
                                                           paragraphLength, baseLevel),
                                &SBParagraphRelease);
            if (paragraph) {
                using Line = std::unique_ptr<const struct _SBLine, decltype(&SBLineRelease)>;
                Line line(SBParagraphCreateLine(paragraph.get(), paragraphOffset, paragraphLength),
                          &SBLineRelease);
                if (line) {
                    const SBUInteger runCount = SBLineGetRunCount(line.get());
                    const SBRun* runs = SBLineGetRunsPtr(line.get());
                    for (SBUInteger index = 0; runs && index < runCount; ++index) {
                        const SBRun& run = runs[index];
                        result.visualRuns.push_back({static_cast<std::uint32_t>(run.offset),
                                                     static_cast<std::uint32_t>(run.length),
                                                     (run.level & 1U) != 0U});
                    }

                    using MirrorLocator = std::unique_ptr<struct _SBMirrorLocator,
                                                          decltype(&SBMirrorLocatorRelease)>;
                    MirrorLocator mirrors(SBMirrorLocatorCreate(), &SBMirrorLocatorRelease);
                    if (mirrors) {
                        SBMirrorLocatorLoadLine(mirrors.get(), line.get(), utf8Text.data());
                        while (SBMirrorLocatorMoveNext(mirrors.get())) {
                            const SBMirrorAgent* agent = SBMirrorLocatorGetAgent(mirrors.get());
                            if (agent) {
                                result.mirroredCodepoints.push_back(
                                    {static_cast<std::uint32_t>(agent->index),
                                     static_cast<char32_t>(agent->mirror)});
                            }
                        }
                    }
                }
            }
        }
        paragraphOffset += paragraphLength + separatorLength;
    }

    std::sort(result.mirroredCodepoints.begin(), result.mirroredCodepoints.end(),
              [](const MirroredCodepoint& left, const MirroredCodepoint& right) {
                  return left.byteOffset < right.byteOffset;
              });
    return result;
}

} // namespace Vixen::Text
