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
methods are not executing while Dobby patches them. The current Geode package
does not meet that guarantee, so it deliberately does not call
`bismuth_install_cocos_hooks`.

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
entire required hook set succeeds, so partial installations remain pass-through
with no observer attached. A process restart is required after a partial
installation failure. This policy prevents unsafe retries; it does not prove
that a partially patched process is safe to continue indefinitely, so the
installer remains experimental and must be exercised only in a controlled
pre-render environment.
