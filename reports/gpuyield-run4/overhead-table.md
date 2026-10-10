Scale 1.0, 500 frames; per-pass GPU-clock spans after excluding 10 warmup frames.

| Resolution / pass | Unbounded ms | Tuned bounded ms | Change |
|---|---:|---:|---:|
| 500×500 / cpu_frame_time_ms | 3.668313 | 17.079836 | +365.6% |
| 500×500 / whole_frame_gpu_span_ms | 0.715966 | 1.557122 | +117.5% |
| 500×500 / esvo_traverse_shade_ms | 0.326041 | 0.349328 | +7.1% |
| 500×500 / direct_lighting_ms | 0.000280 | 0.000280 | -0.1% |
| 500×500 / spatial_reuse_ms | 0.054153 | 0.066295 | +22.4% |
| 500×500 / probe_gather_ms | 0.000280 | 0.000280 | -0.1% |
| 500×500 / shadow_visibility_wave_ms | 0.048817 | 0.053722 | +10.0% |
| 500×500 / shadow_ray_trace_ms | 0.000281 | 0.000279 | -0.5% |
| 500×500 / probe_apply_ms | 0.000281 | 0.000280 | -0.2% |
| 500×500 / render_target_blit_ms | 0.006257 | 0.010160 | +62.4% |
| 500×500 / ui_composite_render_ms | 0.033164 | 0.044598 | +34.5% |
| 1920×1080 / cpu_frame_time_ms | 4.434962 | 17.057190 | +284.6% |
| 1920×1080 / whole_frame_gpu_span_ms | 3.643272 | 4.104231 | +12.7% |
| 1920×1080 / esvo_traverse_shade_ms | 1.706686 | 2.041262 | +19.6% |
| 1920×1080 / direct_lighting_ms | 0.000280 | 0.000280 | +0.1% |
| 1920×1080 / spatial_reuse_ms | 0.732949 | 0.738185 | +0.7% |
| 1920×1080 / probe_gather_ms | 0.000280 | 0.000280 | +0.0% |
| 1920×1080 / shadow_visibility_wave_ms | 0.752600 | 0.729210 | -3.1% |
| 1920×1080 / shadow_ray_trace_ms | 0.000280 | 0.000280 | +0.1% |
| 1920×1080 / probe_apply_ms | 0.000280 | 0.000280 | +0.1% |
| 1920×1080 / render_target_blit_ms | 0.034265 | 0.034965 | +2.0% |
| 1920×1080 / ui_composite_render_ms | 0.036848 | 0.038376 | +4.1% |
