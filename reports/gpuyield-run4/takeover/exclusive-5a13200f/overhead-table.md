Scale 1.0, 500 frames; per-pass GPU-clock spans after excluding 10 warmup frames.

| Resolution / pass | Unbounded ms | Tuned bounded ms | Change |
|---|---:|---:|---:|
| 500×500 / cpu_frame_time_ms | 3.990267 | 17.070248 | +327.8% |
| 500×500 / whole_frame_gpu_span_ms | 0.841131 | 1.483495 | +76.4% |
| 500×500 / esvo_traverse_shade_ms | 0.338710 | 0.373860 | +10.4% |
| 500×500 / direct_lighting_ms | 0.000280 | 0.000279 | -0.3% |
| 500×500 / spatial_reuse_ms | 0.080104 | 0.092485 | +15.5% |
| 500×500 / probe_gather_ms | 0.000280 | 0.000280 | +0.0% |
| 500×500 / shadow_visibility_wave_ms | 0.054579 | 0.058742 | +7.6% |
| 500×500 / shadow_ray_trace_ms | 0.000280 | 0.000280 | +0.0% |
| 500×500 / probe_apply_ms | 0.000281 | 0.000280 | -0.3% |
| 500×500 / render_target_blit_ms | 0.006249 | 0.009839 | +57.5% |
| 500×500 / ui_composite_render_ms | 0.032565 | 0.047809 | +46.8% |
| 1920×1080 / cpu_frame_time_ms | 4.742159 | 17.048169 | +259.5% |
| 1920×1080 / whole_frame_gpu_span_ms | 3.964882 | 4.425500 | +11.6% |
| 1920×1080 / esvo_traverse_shade_ms | 1.898680 | 2.104950 | +10.9% |
| 1920×1080 / direct_lighting_ms | 0.000280 | 0.000279 | -0.3% |
| 1920×1080 / spatial_reuse_ms | 0.763877 | 0.760825 | -0.4% |
| 1920×1080 / probe_gather_ms | 0.000280 | 0.000280 | -0.0% |
| 1920×1080 / shadow_visibility_wave_ms | 0.828569 | 0.787251 | -5.0% |
| 1920×1080 / shadow_ray_trace_ms | 0.000280 | 0.000280 | -0.2% |
| 1920×1080 / probe_apply_ms | 0.000280 | 0.000280 | -0.1% |
| 1920×1080 / render_target_blit_ms | 0.033142 | 0.033723 | +1.8% |
| 1920×1080 / ui_composite_render_ms | 0.038030 | 0.041846 | +10.0% |
