#include <array>
#include <cassert>
#include <cmath>

struct HistoryResult {
    std::array<float, 2> current;
    std::array<float, 2> storedHistory;
};

static HistoryResult MirrorInPlaceHistory(const std::array<float, 2>& initialHistory,
                                          const std::array<float, 2>& currentRadiance,
                                          float alpha) {
    std::array<float, 2> history = initialHistory;
    // Invocation 0 reprojects from texel 1, then stores texel 0.
    const float invocation0 = history[1] * (1.0f - alpha) + currentRadiance[0] * alpha;
    history[0] = invocation0;

    // Invocation 1 reprojects from texel 0. In-place storage may expose invocation 0's
    // same-dispatch write here instead of the previous frame's value.
    const float invocation1 = history[0] * (1.0f - alpha) + currentRadiance[1] * alpha;
    history[1] = invocation1;
    return {{invocation0, invocation1}, history};
}

static HistoryResult MirrorSeparatedHistory(const std::array<float, 2>& previousFrame,
                                            const std::array<float, 2>& currentRadiance,
                                            float alpha) {
    std::array<float, 2> current{};
    current[0] = previousFrame[1] * (1.0f - alpha) + currentRadiance[0] * alpha;
    current[1] = previousFrame[0] * (1.0f - alpha) + currentRadiance[1] * alpha;
    return {current, current};
}

int main() {
    const std::array<float, 2> previousFrame{10.0f, 20.0f};
    const std::array<float, 2> currentRadiance{100.0f, 200.0f};
    const float alpha = 0.5f;

    const HistoryResult racy = MirrorInPlaceHistory(previousFrame, currentRadiance, alpha);
    assert(std::fabs(racy.current[1] - 130.0f) < 1e-6f);

    const HistoryResult result = MirrorSeparatedHistory(previousFrame, currentRadiance, alpha);
    assert(std::fabs(result.current[0] - 60.0f) < 1e-6f);
    // Correct output uses previousFrame[0] (10), not the just-written value 60.
    assert(std::fabs(result.current[1] - 105.0f) < 1e-6f);
    assert(result.storedHistory[0] == result.current[0]);
    assert(result.storedHistory[1] == result.current[1]);
    return 0;
}
