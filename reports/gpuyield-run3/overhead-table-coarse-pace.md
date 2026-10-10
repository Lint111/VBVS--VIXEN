Scale 1.0, 500 frames; per-pass GPU-clock spans after excluding 10 warmup frames.

| Resolution / pass | Unbounded ms | Tuned bounded ms | Change |
|---|---:|---:|---:|
| 500×500 / cpu_frame_time_ms | 4.388762 | 31.436691 | +616.3% |
| 500×500 / whole_frame_gpu_span_ms | 0.945043 | 1.193271 | +26.3% |
| 500×500 / esvo_traverse_shade_ms | 0.337087 | 0.366588 | +8.8% |
| 500×500 / direct_lighting_ms | 0.000280 | 0.000280 | -0.2% |
| 500×500 / spatial_reuse_ms | 0.065224 | 0.078232 | +19.9% |
| 500×500 / probe_gather_ms | 0.000280 | 0.000280 | -0.1% |
| 500×500 / shadow_visibility_wave_ms | 0.052609 | 0.067572 | +28.4% |
| 500×500 / shadow_ray_trace_ms | 0.000280 | 0.000280 | -0.2% |
| 500×500 / probe_apply_ms | 0.000281 | 0.000280 | -0.3% |
| 500×500 / render_target_blit_ms | 0.006421 | 0.008052 | +25.4% |
| 500×500 / ui_composite_render_ms | 0.032687 | 0.039848 | +21.9% |
| 1920×1080 / cpu_frame_time_ms | 5.079638 | 31.404069 | +518.2% |
| 1920×1080 / whole_frame_gpu_span_ms | 3.934913 | 4.540448 | +15.4% |
| 1920×1080 / esvo_traverse_shade_ms | 1.838277 | 2.205455 | +20.0% |
| 1920×1080 / direct_lighting_ms | 0.000280 | 0.000279 | -0.4% |
| 1920×1080 / spatial_reuse_ms | 0.745820 | 0.775138 | +3.9% |
| 1920×1080 / probe_gather_ms | 0.000281 | 0.000280 | -0.6% |
| 1920×1080 / shadow_visibility_wave_ms | 0.843663 | 0.838212 | -0.6% |
| 1920×1080 / shadow_ray_trace_ms | 0.000281 | 0.000280 | -0.3% |
| 1920×1080 / probe_apply_ms | 0.000280 | 0.000280 | -0.3% |
| 1920×1080 / render_target_blit_ms | 0.032601 | 0.033638 | +3.2% |
| 1920×1080 / ui_composite_render_ms | 0.038272 | 0.042713 | +11.6% |
