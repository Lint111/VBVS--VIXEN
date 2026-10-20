#ifndef EXPOSURE_HISTOGRAM_GLSL
#define EXPOSURE_HISTOGRAM_GLSL

// Shared by ExposureMeter.comp (fills the histogram) and ExposureReduce.comp
// (reads, adapts and clears it). Bins cover log2 luminance [-20, +12) in
// 0.25 EV steps; out-of-range luminance lands in the edge bins and black or
// sub-epsilon pixels land in bin 0, so the low percentile rejects them.
const uint kExposureHistogramBins = 128u;
const float kExposureHistogramLog2Min = -20.0;
const float kExposureHistogramBinsPerEV = 4.0;
// ExposureMeterResult.state once log2Exposure holds an adapted value.
const uint kExposureStateAdapted = 0x45585031u;

#endif // EXPOSURE_HISTOGRAM_GLSL
