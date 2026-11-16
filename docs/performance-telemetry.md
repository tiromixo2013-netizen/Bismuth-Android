# Performance telemetry: first measurement layer

The passive observer now records the CPU wall-clock duration of the original
`CCSpriteBatchNode::draw()` call.

## Exported fields

- `batch_draw_calls`: number of observed batch-draw calls in the current frame.
- `batch_draw_cpu_ns_total`: sum of measured CPU wall-clock nanoseconds across those calls.
- `batch_draw_cpu_ns_max`: longest individual observed batch-draw call in nanoseconds.
- Existing atlas counters continue to report transform-related writes, structural
  mutations, and dirty-upload observations.

The timer surrounds the original engine function. It therefore measures CPU time
spent inside that call, including CPU-side transform work and driver submission
overhead. It does **not** measure GPU execution time, GPU occupancy, display
presentation time, or end-to-end frame time.

The timer uses `std::chrono::steady_clock` and adds no GL calls, readback,
allocation, or synchronization to the render hook. The values reset when
`bismuth_observer_begin_frame()` is called. Until a verified frame-boundary
hook calls that function, the observer values are cumulative within the current
observer window rather than guaranteed per-display-frame statistics.

## Next instrumentation stages

1. Resolve a safe frame boundary and report frame intervals/p50/p95/p99.
2. Add non-blocking GPU timer-query support only where the target GLES driver
   exposes a supported timer-query extension; never use `glFinish()` for timing.
3. Record CPU submission and GPU execution separately, then compare enabled vs.
   disabled paths on the same device and level.

No performance improvement is claimed from instrumentation alone.
