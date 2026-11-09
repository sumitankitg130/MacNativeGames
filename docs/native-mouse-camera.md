# Direct mouse camera for TotK 1.4.3 on Mac

With Mouse enabled in Settings → Input, movement turns the game camera directly
instead of synthesizing a right stick or gyro. Sensitivity and inversion use the
existing controls. Mouse buttons and custom button prompts keep their bindings.
The ability wheel still uses the mouse to point the right stick at an ability.
The feature is limited to TotK 1.4.3 on macOS; other games and versions retain
their existing input path. `TOTK_NATIVE_MOUSE_CAMERA=0` restores that path at
launch. This is camera control, not a mouse cursor for the game's menus.

Adapted from [dxnside's contribution, commit 366aa078](https://github.com/dxnside/MacNativeGames/commit/366aa07819fe595d5bd3d66fd17a29d4eca0fdb7)
under MIT. Attribution and its license are in `THIRD_PARTY_NOTICES.md` and
`games/totk/third_party/native_mouse_camera/LICENSE.txt`.

## Integration

`runtime/mouse_camera_v143.h` holds the four camera layouts and checked hooks.
`gamepad.m` routes mouse events to them, retaining ability-wheel stick selection,
mouse-button bindings, sensitivity, and prompt activity tracking. AppKit deltas
retain their fractional precision. A mutex protects paired deltas and settings;
the camera thread consumes them once, with no camera pointer kept between calls.
Movement older than 125 ms is discarded; the first movement after resting the
mouse is retained. Releasing capture or entering the wheel clears queued input.

All four original instructions and the entire hook reservation are checked
before any camera branch is written. Veneers occupy the executable tail after
the first 640 bytes reserved for effects/survey patches, and before the last
128 bytes reserved for VR hooks. The font-cache prompt hook uses text padding
elsewhere. Insufficient tail space retains the existing mouse path. An occupied
reservation or mismatched instruction stops loading with an error.

Each veneer preserves integer and full SIMD registers, NZCV, FPCR, and FPSR;
the callback runs through the runtime's host-stack wrapper. The displaced
instruction then executes and returns to its original continuation. Mac callback
addresses are excluded from `TOTK_EXPORT_IOS`, including an export run on Mac.
This prevents embedding a Mac-only callback into an iPhone/iPad game image.

## Verification

`tests/test_native_mouse.py` builds a synthetic camera probe and executes a
generated ARM64 veneer on Apple silicon. It covers all four layouts, Recall's
alternate yaw, pitch bounds, yaw wrapping, fractional input, sensitivity,
inversion, idle/stale input, clearing, concurrent producers, instruction guards,
reservation boundaries, fallback, and iOS export exclusion. It uses no game
files. The mouse probe also checks direct-camera versus wheel routing, sensitivity
changes, capture release, and keyboard/mouse prompt selection.

For local integration tests, `TOTK_INPUT_FILE` accepts `mousemove <dx> <dy>`
(finite AppKit deltas within ±8192). The frontend posts them on its main queue
only while the window actually captures the pointer. This exercises relative
input when a UI automation tool supplies absolute cursor positions without
relative motion. `TOTK_NATIVE_MOUSE_LOG=1` logs the camera kind, consumed delta
in degrees, and resulting angles. Both diagnostics are off in ordinary play.

These tests verify layout handling and input routing, not every gameplay state.
Actual gameplay checks and their limits are recorded with this branch's test
results; physical controller and VR-headset behavior require separate testing.

### Local gameplay validation

Checked on macOS 26.5.1, Apple silicon, TotK 1.4.3, in the Room of Awakening
at a 3840×2160 render size. Tests used a copied profile and controls with cloud
sync explicitly off. A separate app was built; the installed game was kept.

- Known relative deltas reached the running game's camera and changed the
  rendered view: 300 movement points at sensitivity 50 gave 30° yaw; switching
  to 100 live gave 20° yaw for 100 points. Vertical deltas also matched their
  expected angles. Camera turns stopped when movement stopped.
- Settings released capture; a relative test event while the panel was open was
  ignored. Closing the panel resumed capture. Mouse on/off and sensitivity
  changes took effect without restarting.
- The configured **F · Examine** gameplay popup remained visible beside the
  altar with the native camera enabled. Menu/loading prompts also used F.
- The native build, actual-game loader probe, code signature, app self-check,
  synthetic camera/ARM64 tests, input command tests, and Settings probes passed.
- The complete repository suite passed: **1,548 tests, 2 skipped, no failures**
  (`python -m unittest discover -s tests -v`, using the configured Python 3.14
  test environment). The final run used an unlocked desktop; earlier environment
  and screen-lock interruptions are not counted as passing runs.

The automated UI tool provided absolute cursor positions without useful relative
deltas, so the camera-angle checks above used the diagnostic input channel. The
user subsequently checked physical mouse movement and confirmed smooth camera
turning. Bow aiming, Ultrahand, Recall and the
ability wheel were not checked in actual gameplay: this save has not unlocked
those features. Their layouts/routing have synthetic coverage. Physical
controllers, a VR headset, and an iOS build were not available for this check.
The diagnostic session used a warm-up list limited to this scene; these results
are functional checks, not a performance benchmark. No screenshots were uploaded.
