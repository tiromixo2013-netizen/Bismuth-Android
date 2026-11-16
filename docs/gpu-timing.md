# Asynchronous GPU timing

## Status

The Android native library now contains an opt-in GPU timer-query ring around
the original `CCTextureAtlas::drawNumberOfQuads` call. It uses
`GL_EXT_disjoint_timer_query` when the current GLES context exposes the
extension and all required entry points. Unsupported drivers fail closed and
continue through the original draw path.

This is GPU execution timing for the commands submitted inside one atlas draw;
it is not whole-frame GPU time, display presentation time, or proof of FPS
improvement.

## Runtime behavior

- Disabled by default. Enable with `bismuth_set_gpu_timing_enabled(1)`.
- The enable call only changes an atomic flag. The next atlas draw initializes
  query objects on the render thread with the game's context current.
- Uses four query objects as a ring. If all are pending, a sample is skipped.
- Polls `GL_QUERY_RESULT_AVAILABLE_EXT` before reading a result. It never waits
  for an unfinished query and never calls `glFinish`.
- Checks `GL_GPU_DISJOINT_EXT`; disjoint intervals are discarded and counted.
- Skips sampling if the game already owns an active elapsed-time query.
- Publishes last, mean, p95 and p99 GPU nanoseconds over a rolling 120-sample
  window, plus disjoint and skipped-sample counts.
- The original Cocos atlas draw remains the operation being measured. The
  timer query does not replace or reorder the draw.

## C API

```c
bismuth_set_gpu_timing_enabled(1);
BismuthGpuTiming timing = {0};
bismuth_gpu_timing_snapshot(&timing);
```

Check `timing.supported` before interpreting values. A zero sample count means
no completed query has been published yet. The API snapshot is safe to read
without issuing GL calls; all query-object operations remain render-thread
owned.

## Limitations

The implementation has not yet been exercised on the Galaxy M14 or any live
Geometry Dash session. Driver support, query overhead, and the measured atlas
GPU interval must be validated on the device before using the numbers for
optimization decisions. The current build has no in-game settings panel yet;
the C API is the control surface until the target PauseLayer UI ABI is
confirmed.

The timer measures atlas draw intervals only. It does not include unrelated
GPU work elsewhere in the frame, composition, or scan-out.
