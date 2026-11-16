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


## Live frame-cadence integration

When the verified Cocos hooks are installed, Bismuth also attempts an optional
hook on the process' exported `libEGL.so!eglSwapBuffers`. At each call it
records a monotonic timestamp immediately before forwarding to the original
EGL function. The 120-sample interval window therefore reports **app buffer-
submission cadence** (mean and p50/p95/p99), not physical display presentation,
GPU completion, or guaranteed compositor timing. The hook is optional and
non-fatal: if the EGL symbol cannot be resolved from `libEGL.so` or Dobby
cannot install it, the original rendering path and Cocos observer hooks remain
available, but frame-cadence samples remain empty.


# Performance optimization batch: hot-path and capacity work

This batch is deliberately separate from Frame Generation, upscaling, and CAS.
It improves the existing standalone GLES compute prototype and C API plumbing;
it does not replace the Geometry Dash renderer or enable the GPU draw bridge.

## Changes

- **Uniform lookup moved out of dispatch:** all five compute-shader uniform
  locations are resolved once after program link and validated during
  initialization. The per-dispatch path now uses cached integer locations
  instead of repeatedly calling `glGetUniformLocation`.
- **Geometric SSBO capacity growth:** candidate and intermediate buffers grow
  in whole 128-thread workgroups using a doubling policy, capped at the existing
  128-workgroup / 16,384-candidate prototype limit. This reduces repeated
  buffer reallocation when candidate counts increase gradually. Capacity does
  not shrink automatically.
- **Reusable C API staging storage:** each `BismuthInstance` retains its
  candidate conversion vector. After the first sufficiently large upload,
  later uploads within that capacity reuse the same allocation rather than
  constructing and destroying a temporary vector each call. Allocation failure
  returns the existing error code instead of allowing an exception across the
  C ABI.
- **Host tests:** the pure capacity-growth policy is tested at initial, doubling,
  stable-capacity, maximum-capacity, and invalid-boundary cases.

## Safety invariants retained

- GLES calls still require the owning context to be current on the calling
  thread.
- Existing compute state save/restore remains intact, including SSBO range
  bindings.
- Existing fail-closed command clearing and atlas-slot validation remain.
- No `glFinish()`, synchronous readback, sticky `glGetError()` polling, or
  frame-boundary guess is introduced.
- The original Cocos draw path and sprite transform loop are untouched.
- The experimental GPU draw bridge remains disabled by default.

## Validation and limits

Host CMake/CTest covers the new capacity policy. Android ARM64 CI compiles the
actual GLES code path, including cached uniform fields and reuse of the staging
vector. These checks do not measure runtime FPS or GPU execution on a physical
phone. The next performance decision still requires A/B captures on the target
Galaxy M14 and Skeletal Shenanigans, with the same scene and thermal conditions.

The current telemetry remains CPU-side observation and EGL buffer-submission
cadence; it is not a GPU timer or display-presentation measurement.
