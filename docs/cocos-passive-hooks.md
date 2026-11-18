# Cocos2d passive hooks and experimental GPU draw bridge

## Scope and current activation state

The Android ARM64 native library contains a passive hook adapter for the exact
Geometry Dash 2.2081 Cocos2d binary identified by GNU Build ID
`ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`.

**Important: the current Geode Android64 package does not install these hooks.**
The Geode integration intentionally runs in safe mode because its Loaded event
may occur after the game render path is active and other mods may already own
hooks on the same Cocos2d methods. The pause-menu settings UI remains available,
but native telemetry and GPU timing are dormant. Do not describe this adapter
as active in the Geode package.

The adapter uses Dobby at pinned commit
`a418c6a7c493e6c599713a097ff823c84a40ba96` (Apache-2.0) for ARM64 inline
trampolines. Its installation routine checks the loaded module's GNU Build ID
and verifies resolved Cocos symbols belong to `libcocos2dcpp.so`. These
checks identify the target binary; they do not prove that a hook is compatible
with other mods or safe to install after rendering has begun.

## Native hook set (experimental; not activated by Geode)

The native adapter contains pass-through wrappers for:
- `CCSpriteBatchNode::draw()`
- `CCSprite::updateTransform()`
- `CCTextureAtlas::updateQuad()`
- `CCTextureAtlas::drawNumberOfQuads(count, start)`
- Atlas insert, remove, move, remove-all, and count mutation methods.

Each wrapper is intended to forward to the original method and record passive
telemetry or conservative cache invalidation. The exact callback signatures,
installation ordering, and interoperability with other mods still require
on-device validation. The former `resizeCapacity` hook is intentionally absent
because it caused a confirmed recursive-hook crash in an earlier build.

The child-count reader follows the target's documented metadata path:
`CCSpriteBatchNode +0xd0 -> CCArray object; CCArray +0x38 -> _ccArray data;
_ccArray +0x00 -> element count`. Host tests cover this pointer-chain layout
using synthetic memory. The reader does not traverse child objects.

## Installation contract

The native API's installation routine is intended only for a controlled host
that can guarantee the exact target library is loaded and the relevant render
methods are not executing while Dobby patches them. The build option
`BISMUTH_ENABLE_COCOS_HOOKS` is now **OFF by default**. With the default
configuration, the adapter translation unit supplies no-op API stubs, Dobby
is not fetched or linked, and `JNI_OnLoad` does not attempt any Cocos detours.
The Geode package also does not call `bismuth_install_cocos_hooks`.

The native GPU draw bridge is also experimental and remains disabled. It must
not replace the game's draw call until the complete render state, VAO/EBO
ownership, synchronization, painter order, and fallback behavior have been
validated on the target device.

## Remaining validation

- Establish a safe pre-render installation point before reactivating any hooks.
- Verify every function signature and Dobby trampoline against the exact binary.
- Test coexistence with node-ids, QOLMod, Better Visuals, and other Cocos hook users.
- Confirm all callbacks execute on the render thread and observer lifetime is safe.
- Validate the atlas child-array metadata reader against runtime objects.
- Calibrate sprite bounds against camera/view coordinates before any culling.
- Preserve the original draw path until alpha, clipping, blend, painter order,
  and device performance are validated.

No FPS improvement or in-game runtime success is claimed by this adapter.

### Partial-install failure policy

The passive installer now enters a poisoned state immediately before its
first Dobby detour attempt. If any later installation step fails, subsequent
install calls in the same process are rejected with an explicit error. This
is intentionally conservative: a failed Dobby call may have changed executable
code even when it did not provide a usable original trampoline, and the
adapter has not established that reverse-order unpatching is safe while game
threads may execute. The observer pointer is still published only after the
entire required hook set succeeds. **This does not guarantee that a partial
installation is pass-through or safe for the game to continue.** Inspection of
the exact pinned Dobby source (`a418c6a7c493e6c599713a097ff823c84a40ba96`)
found that `DobbyHook` calls `routing->Active()` (which patches the target)
before checking `routing->error`. If that later error check fails, Dobby returns
an error before publishing the original trampoline and before registering the
entry in its interceptor table. In that failure shape, the target may already
be patched, the wrapper may have no original-function pointer, and
`DobbyDestroy(target)` cannot find the unregistered entry. The current
installer's poisoned state prevents a second attempt but cannot repair this
first partial failure. A process restart is required, and the game process
must be treated as potentially compromised after such a failure. Until the
pinned Dobby failure path is patched and tested, this adapter must not be
enabled in a normal player session; use only a controlled test environment.


### Pinned Dobby failure-path blocker (2026-11-18)

The pinned dependency's source was inspected, not inferred from its header:

- `source/InterceptRouting/InlineHookRouting.h`: `BuildRouting()` calls
  `GenerateTrampoline()`, `GenerateRelocatedCode()`, and `BackupOriginCode()` without stopping when relocation has set `error`.
- `source/InterceptRouting/InterceptRouting.h`: `Active()` calls
  `DobbyCodePatch()` and only ORs its result into `error`.
- `source/dobby.cpp`: `DobbyHook()` calls `routing->BuildRouting()`, then
  `routing->Active()`, and only afterwards checks `routing->error`. On error it
  returns before writing `out_origin_func` and before `gInterceptor.add(entry)`.
- `DobbyDestroy()` searches the interceptor table. An entry rejected before
  `gInterceptor.add(entry)` is not discoverable by that API.

Therefore the installer's poison flag is only a retry guard, not a recovery
mechanism. A proper fix belongs in the pinned Dobby dependency: fail before
`Active()` whenever trampoline generation or relocation fails, and ensure any
post-patch failure restores the original bytes before returning. This must be
implemented and tested in the dependency before the native adapter is
considered safe for ordinary gameplay. No new Cocos hooks or transform cache
are authorized by this finding.


### Safe default after Dobby audit (2026-11-18)

The default CMake configuration now sets `BISMUTH_ENABLE_COCOS_HOOKS=OFF`.
The native library still builds its non-hook GLES/core pieces, but the Cocos
adapter is compiled in stub mode: it does not include Dobby headers, does not
link Dobby, and reports hook installation as unavailable. `JNI_OnLoad` records
a disabled status instead of attempting detours. The capability bitmask omits
Cocos hooks, GPU draw bridge, and GPU timer-query capabilities in this mode.
To opt in, a developer must explicitly set `-DBISMUTH_ENABLE_COCOS_HOOKS=ON`;
that mode remains experimental and unsafe until the pinned Dobby failure path
is patched and regression-tested. This change is a safety containment, not a
repair of Dobby's internals and not a renderer optimization.


### Pinned Dobby failure-path patch (2026-11-18)

A maintained source patch is now applied to the pinned Dobby checkout by
cmake/patch_dobby.cmake through FetchContent's PATCH_COMMAND. It is applied
only when a developer explicitly configures
-DBISMUTH_ENABLE_COCOS_HOOKS=ON; the safe default remains OFF.

The patch makes routing construction fail closed:
- A missing trampoline buffer is detected before relocation or target patching.
- Relocation must produce both a non-empty overwritten-instruction range and a
  non-empty relocated trampoline.
- The original target bytes must be backed up before Active() can patch them.
- DobbyHook and DobbyInstrument stop immediately on any preparation error;
  they do not call Active() after a failed build.
- The optional original-trampoline output is initialized to null and is only
  populated/signed when the caller supplied an output pointer.
- DobbyDestroy keeps the entry registered if restoring original bytes fails,
  allowing a later retry instead of losing the recovery record.

The Android/POSIX code writer now checks mprotect before writing. If either
page cannot be made writable, it returns failure before copying any target
bytes; the second-page failure path attempts to restore the first page's RX
permissions. Once bytes have been copied, the function reports the detour as
active even if restoring RX permissions fails, and logs that condition. This
avoids the especially dangerous false-failure state where a live detour is
reported as failed and never registered, but it means a page can remain RWX if
the permission restore itself fails. The implementation still assumes the
target code pages should be RX when restored, as the previous implementation
did.

This is a source-level failure-ordering repair, not proof of full hook safety.
It does not make patching atomic against another thread executing the target,
does not stop the game/render threads, and does not validate Dobby trampoline
coexistence with other mods. The Cocos hook option therefore remains OFF by
default. No transform cache or render fast path is enabled.

Validation status: the patch script and FetchContent wiring are committed,
but an Android ARM64 build and fault-injection tests have not yet run. Required
regression cases are: trampoline allocation failure, relocation failure,
original-byte backup failure, first-page mprotect failure, second-page
mprotect failure, successful hook + original trampoline call-through, and
successful/failed DobbyDestroy restoration. Do not enable this in a normal
player session until those checks and on-device coexistence tests pass.
