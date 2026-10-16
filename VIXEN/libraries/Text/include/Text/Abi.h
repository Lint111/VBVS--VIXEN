#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Vixen::Text {

// Pack-local ordinals keep preset metadata typed without sending locale strings
// through the renderer boundary. Only TextArena carries UTF-8 glyph content.
enum class TextDirection : std::uint8_t { LeftToRight, RightToLeft };
enum class ShapingClass : std::uint8_t { Default, Joining, Indic, Cluster };
enum class LineBreakingClass : std::uint8_t { Word, Phrase, Dictionary };
enum class CasingBehavior : std::uint8_t { LocaleSensitive, None, Turkic };
enum class TruncationConvention : std::uint8_t { Ellipsis, Cut };

struct ProgramSlice {
    std::uint32_t offset;
    std::uint32_t byteCount;
};

struct TextRef {
    std::uint32_t key;
    std::uint32_t structure;
};

struct PresetDesc {
    std::uint32_t localeOrdinal;
    std::uint32_t fallbackPresetOrdinal;
    std::uint32_t layoutPresetOrdinal;
    std::uint32_t inputPresetOrdinal;
    std::uint32_t numberDateLocaleOrdinal;
    std::uint32_t requiredScriptOrdinalOffset;
    std::uint32_t requiredScriptOrdinalCount;
    std::uint32_t pluralCategoryOrdinalOffset;
    std::uint32_t pluralCategoryOrdinalCount;
    std::uint32_t genderCategoryOrdinalOffset;
    std::uint32_t genderCategoryOrdinalCount;
    std::uint32_t fontFallbackOrdinalOffset;
    std::uint32_t fontFallbackOrdinalCount;
    ProgramSlice pluralProgram;
    ProgramSlice numberDatePatternProgram;
    TextDirection direction;
    ShapingClass shapingClass;
    LineBreakingClass lineBreakingClass;
    CasingBehavior casingBehavior;
    TruncationConvention truncation;
    float minimumFontSizePt;
    float expansionFactor;
};

struct TextArena {
    const char* utf8Bytes;
    const std::uint32_t* rowOffsets;
    std::uint32_t byteCount;
    std::uint32_t rowCount;
};

enum class TextElementKind : std::uint8_t { ConventionTag, AuthoredTag, Hook, Placeholder };

// This matches the two-table bake contract: an element is language independent,
// while a span places that element in one preset's UTF-8 row.
struct TextElement {
    TextElementKind kind;
    std::uint8_t reserved;
    std::uint16_t typeId;
    std::uint32_t payload;
};

struct TextSpan {
    std::uint16_t element;
    std::uint32_t byteStart;
    std::uint32_t byteEnd;
};

struct TextElementSet {
    std::uint16_t count;
    std::uint16_t reserved; // zero; aligns the view pointer after the u16 table count
    const TextElement* elements;
};

struct TextPlacement {
    std::uint16_t count;
    std::uint16_t reserved; // zero; aligns the view pointer after the u16 table count
    const TextSpan* spans;
};

struct GlyphInstance {
    float x;
    float y;
    float w;
    float h;
    float u0;
    float v0;
    float u1;
    float v1;
    std::uint32_t span;
    std::uint32_t cluster;
    float msdfRange;
    std::uint32_t flags;
};

struct SpanStyle {
    std::uint32_t rgba;
    float weight;
    float italic;
    std::uint32_t effect;
};

struct SpanAnim {
    std::uint32_t kind;
    float t0;
    float duration;
    float amplitude;
    float frequency;
    float phase;
};

enum class TextHitPhase : std::uint8_t { Enter, Leave, Click };

struct TextHitEvent {
    std::uint32_t key;
    std::uint16_t element;
    TextHitPhase phase;
    std::uint8_t reserved;
    std::uint32_t pointer;
};

static_assert(sizeof(TextRef) == 8);
static_assert(std::is_standard_layout_v<TextRef> && std::is_trivially_copyable_v<TextRef>);
static_assert(sizeof(TextElement) == 8);
static_assert(std::is_standard_layout_v<TextElement> && std::is_trivially_copyable_v<TextElement>);
static_assert(sizeof(TextSpan) == 12);
static_assert(std::is_standard_layout_v<TextSpan> && std::is_trivially_copyable_v<TextSpan>);
static_assert(sizeof(TextElementSet) == 16);
static_assert(std::is_standard_layout_v<TextElementSet> && std::is_trivially_copyable_v<TextElementSet>);
static_assert(sizeof(TextPlacement) == 16);
static_assert(std::is_standard_layout_v<TextPlacement> && std::is_trivially_copyable_v<TextPlacement>);
static_assert(sizeof(GlyphInstance) == 48);
static_assert(std::is_standard_layout_v<GlyphInstance> && std::is_trivially_copyable_v<GlyphInstance>);
static_assert(sizeof(SpanStyle) == 16);
static_assert(std::is_standard_layout_v<SpanStyle> && std::is_trivially_copyable_v<SpanStyle>);
static_assert(sizeof(SpanAnim) == 24);
static_assert(std::is_standard_layout_v<SpanAnim> && std::is_trivially_copyable_v<SpanAnim>);
static_assert(std::is_standard_layout_v<PresetDesc> && std::is_trivially_copyable_v<PresetDesc>);
static_assert(std::is_standard_layout_v<TextArena> && std::is_trivially_copyable_v<TextArena>);
static_assert(std::is_standard_layout_v<TextHitEvent> && std::is_trivially_copyable_v<TextHitEvent>);

} // namespace Vixen::Text
