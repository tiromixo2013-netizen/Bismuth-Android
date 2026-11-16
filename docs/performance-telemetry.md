# Performance telemetry: first measurement layer

The passive observer now records the CPU wall-clock duration of the original
`CCSpriteBatchNode::draw()` call.

## Exported fields

- `batch_draw_calls`: number of observed batch-draw calls in the current observer window. It becomes a per-frame count only when `bismuth_observer_begin_frame()` is called at a verified frame boundary.
- `batch_draw_cpu_ns_total`: sum of measured CPU wall-clock nanoseconds across those calls.
- `batch_draw_cpu_ns_max`: longest individual observed batch-draw call in nanoseconds.
- `batch_draw_children_total`: sum of child-array entries observed across batch-draw calls; divide by `batch_draw_calls` for the average batch size when the count is nonzero.
- Existing atlas counters continue to report transform-related writes, structural
  mutations, and dirty-upload observations.

The timer surrounds the original engine function. It therefore measures CPU time
spent inside that call, including CPU-side transform work and driver submission
overhead. It does **not** measure GPU execution time, GPU occupancy, display
presentation time, or end-to-end frame time.

The timer uses `std::chrono::steady_clock` and adds no GL calls, readback,
allocation, or synchronization to the render hook. The values reset when
`bismuth_observer_begin_frame()` is called. The current hook does not yet call
that function at a verified frame boundary, so the observer values are cumulative
within the current observer window rather than guaranteed per-display-frame statistics.

## Next instrumentation stages

1. Resolve a safe frame boundary and report frame intervals/p50/p95/p99.
2. Add non-blocking GPU timer-query support only where the target GLES driver
   exposes a supported timer-query extension; never use `glFinish()` for timing.
3. Record CPU submission and GPU execution separately, then compare enabled vs.
   disabled paths on the same device and level.

No performance improvement is claimed from instrumentation alone.


## Frame interval window (prepared API)

A fixed-size, allocation-free 120-sample rolling window is available through
`FrameIntervalWindow`. The native API
`bismuth_observer_record_frame_start(handle, monotonic_ns)` accepts one
monotonic frame-start timestamp and exposes sample count, last/mean interval,
and nearest-rank p50/p95/p99 interval values through the telemetry snapshot.
Non-monotonic timestamps are rejected.

**Integration is not yet connected to a verified game frame boundary.** The
current Cocos hook measures the duration of the original batch-draw call, not
the complete frame. Do not feed batch-draw entry/exit timestamps as frame
starts, and do not label these statistics as FPS until a reliable frame-start
callback is integrated and tested. At present this API is testable with
synthetic timestamps only.


## Verified batch child-count observation

The batch-draw hook now samples the child-array count before calling the original
`CCSpriteBatchNode::draw()` function. For the one target ELF Build ID accepted
by the hook, static disassembly established the child-array pointer at
`CCSpriteBatchNode + 0x148` and its count field at
`childArray + 0x38`. The reader performs two bounded metadata copies and does
not walk child pointers or alter game state. A null pointer or implausible count
fails closed to zero.

`max_children_in_batch` is therefore now populated from the observed batch
metadata. It is a child-entry count, not a count of successful `updateTransform()`
calls: null entries and subclass dispatch may make those differ. The timing
counter still measures the complete CPU wall time of the original batch draw,
including the per-child virtual-call loop and atlas submission overhead.


## Optional Perfetto / Android ATrace sections

Configure an Android profiling build with
`-DBISMUTH_ENABLE_ATRACE=ON`. When a system trace is actively recording,
the hook emits the slices `Bismuth/BatchDraw` and
`Bismuth/GPUBridge`. The GPUBridge slice measures CPU-side setup and
submission time only; it is not GPU execution duration. The option is OFF by
default and adds no ATrace calls to normal builds. Android CI compiles the
profiling-enabled variant to keep the optional instrumentation buildable.
