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


## Sprite transform-call observation

The verified target exports `CCSprite::updateTransform()`. The passive hook now
forwards to the original method first, then increments
`sprite_transform_calls` when passive telemetry is enabled. It records calls
that reach this base implementation; virtual overrides that replace the base
method entirely are not counted, so this is a lower-bound observation rather
than a complete count of every child transform dispatch. The hook does not skip,
modify, or defer any transform work. It is intended to quantify the CPU-side
per-child transform cost before attempting any visibility optimization.

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


## Runtime telemetry gate (0.8.0)

The C ABI exposes `bismuth_set_passive_telemetry_enabled(0|1)` and
`bismuth_is_passive_telemetry_enabled()`. Passive telemetry is enabled by
default so existing counters continue to work. A host/loader can turn it off
for a lower-overhead comparison run, then turn it back on without reinstalling
hooks or restarting the game.

When disabled, passive Cocos hooks skip batch child-count reads, clock
sampling, ATrace batch sections, dirty-byte inspection, and observer counter
updates. The EGL swap hook forwards directly to the original function without
taking a timestamp. The experimental GPU draw bridge is controlled separately
and remains disabled by default.
When the GPU bridge is disabled, the atlas draw hook also skips saving and restoring its thread-local bridge context; it forwards directly through the original atlas draw path while retaining only the dirty-byte observation when telemetry is enabled.

This switch does **not** remove already-installed Dobby trampolines, so it is
not a perfectly uninstrumented baseline: the pass-through detour cost remains.
Use it to compare measurement-work overhead, not to claim zero-hook overhead.
The switch uses a relaxed atomic flag and does not publish or guard observer
memory; callers must still obey observer lifetime and render-thread rules.


## Lazy GPU draw-bridge hook installation

The passive Cocos hook installer no longer installs the process-wide
`glDrawElements` detour. That detour is installed on the first explicit
`bismuth_set_gpu_draw_bridge_enabled(1)` request. The bridge is marked enabled
only after Dobby reports a successful hook and returns the original function.
If symbol resolution or hook installation fails, the bridge remains disabled
and the normal Cocos/GLES path is preserved. Disabling the bridge keeps the
installed trampoline in place (safe process-lifetime behavior), but the detour
immediately forwards to the original GLES function. Thus a normal passive
telemetry session has no Bismuth `glDrawElements` detour at all.

This is a code-path reduction, not a measured FPS claim. The first opt-in
activation performs symbol lookup and hook installation and should be done
before controlled gameplay/render stress, not in a frame-critical callback.


## Observer-aware measurement fast path

The batch-draw and EGL-swap hooks now acquire the observer before doing
measurement-only work. If no observer is attached, batch drawing forwards
directly to the original Cocos function without reading child metadata or
sampling the clock; EGL swap forwards without taking a timestamp. The observer
reference remains protected by the existing active-callback lifetime counter
while a batch measurement is in progress. Empty atlas draw ranges also skip
the dirty-byte read because they cannot trigger the existing non-empty upload
invalidation record.

These changes remove avoidable CPU-side measurement work in detached-observer
and empty-draw cases. They do not bypass Cocos rendering, alter draw ordering,
or claim a measurable FPS gain without device A/B testing.


## Null-observer acquisition fast path

Observer callback acquisition first performs an acquire-load of the observer
pointer. When no observer is attached, it returns immediately without changing
the active-callback counter. If a pointer is present, it increments the active
counter and rechecks the pointer before returning it; a concurrent detach that
wins the race causes the acquisition to back out without exposing the pointer.
This preserves the detach lifetime protocol while removing an atomic
read-modify-write pair from the common no-observer path.


## Paired observer release guard

The callback release helper now accepts the acquired observer pointer and only decrements the active-callback count when acquisition returned a non-null observer. This keeps the null-observer fast path balanced: a failed acquisition performs neither increment nor decrement, while a successful acquisition performs exactly one of each.


---

## Geode live HUD and Advanced Monitor

The safe-mode Geode UI now has a separate gameplay HUD. It deliberately does
not install the native Cocos observer hooks.

- The displayed FPS is derived from the `PlayLayer::update(float dt)` delta
  and is labelled **game-loop FPS**. It is not physical display-present FPS.
- Frame time is the same update delta in milliseconds.
- CPU time is the wall-clock duration of the original
  `PlayLayer::update` call, measured around the call. It is not total process
  CPU utilization.
- The rolling 120-sample window reports nearest-rank p50/p95/p99 update
  intervals. The displayed `1% LOW*` is an estimate derived from p99; it is
  not a platform frame-pacing measurement.
- The hitch counter counts update intervals above 33.3 ms. It is a simple
  threshold counter, not a diagnosis of the cause of a hitch.
- The optional 24-character graph visualizes recent update intervals:
  `.` below 16.7 ms, `-` from 16.7 to 25 ms, `=` from 25 to 33.3 ms,
  and `#` at or above 33.3 ms.
- Text refresh is throttled to 4 Hz; the rolling samples are still recorded
  on each valid PlayLayer update.
- GPU usage, GPU execution time, draw calls, RAM, texture memory, and vertex
  counts remain unavailable in this Geode HUD. They must not be filled with
  estimates presented as measured values.

### Crash-path precaution

The earlier crash report recorded SIGSEGV in `libc memcpy` from
`CCSpriteBatchNode::appendChild` while `CCLabelBMFont` was building
level-page text. The trace also included `node-ids` and `QOLMod`, but did not
prove a unique culprit. To avoid adding another BMFont sprite-child batch to
that same path, the Geode HUD uses a standalone `CCLabelTTF` and throttles
texture/string refreshes. This reduces overlap with the recorded failure path;
it does not establish a root-cause fix. The original level-page scrolling
reproduction remains a required on-device crash test.


## Process resident-memory HUD metric

The Geode HUD optionally reads the second field of `/proc/self/statm` at the
existing 4 Hz text-refresh cadence and converts resident pages using
`sysconf(_SC_PAGESIZE)`. It is labelled **Process RSS** because it includes
resident pages for the whole process; it is not a native-heap-only measurement
and does not identify allocations by subsystem. If procfs access or parsing
fails, the HUD displays N/A. The metric can be disabled with the
`hud-memory` setting. This sampling does not allocate GL resources or perform
GPU readback.
