# Repository instructions and session notes

## Scope and workflow

- Read [CONTRIBUTING.md](CONTRIBUTING.md) before implementation. Follow the user's
  current instructions when they override repository workflow preferences.
- This checkout is the user's MacNativeGames fork. The user requested that future
  fixes go directly to `main`; do not create a feature branch by default.
- Fork: https://github.com/sumitankitg130/MacNativeGames (`fork` remote here).
  Upstream: https://github.com/ly05010419/MacNativeGames (`origin` here).
  Inspect remote URLs before pushing; never assume `origin` is the user's fork.
- Local `main` tracks `fork/main`, with `branch.main.pushRemote=fork`. Publish
  authorized, validated changes there without force-pushing or discarding work.
  Do not open, merge, close, or comment on upstream PRs unless requested.
- Make focused changes, retain third-party attribution, and report what was
  actually tested. A documentation-only change does not require rebuilding the app.

## Current baseline

As of 2026-11-09, the fork's `main` contains both:

- `207da864`: live configured button prompts.
- `f6007835`: direct mouse camera, adapted from dxnside's commit
  `366aa07819fe595d5bd3d66fd17a29d4eca0fdb7` under MIT.

The main `Tears of the Kingdom.app` was initially updated to this combined runtime,
with loader, signature, and startup checks passing (source label `f600783`).
Read its current `MNGSourceCommit` after later updates; this is a historical baseline.
Mouse was **on** and Button prompts **Auto** at verification. Preserve the user's
current settings rather than resetting them to these historical values.

The upstream prompt-only PR is https://github.com/ly05010419/MacNativeGames/pull/1940.
Its screenshots and the dedicated screenshot release were removed at the user's
request. **Do not publish gameplay screenshots again without explicit authorization.**
Local captures may be used for validation. No game content belongs in commits.

## Protect game content, settings, and saves

- Never commit or upload game containers, keys, firmware, extracted resources,
  rewritten game modules, saves, translated shaders/libraries, or playable apps.
  Keep local build/test output under ignored work directories. Inspect staged
  filenames before committing; use the repository's content-checking git hooks.
- Preserve current saves. The user explicitly chose to keep them after an earlier
  test inherited cloud sync; do not restore old snapshots as cleanup.
- Test gameplay with a **copied profile**, isolated controls, and cloud sync
  explicitly disabled. A copied profile alone does not disable inherited sync.
- Use these launch overrides for isolated tests:

  | Variable | Value/purpose |
  | --- | --- |
  | `TOTK_PROFILE_DIR` | Copied test profile |
  | `TOTK_CONTROLS` | Isolated `controls.txt`; adjacent files hold other input/settings choices |
  | `TOTK_CLOUD_SAVE_DIR` | `0` to disable sync |
  | `TOTK_INPUT_FILE` | Separate diagnostic command file |
  | `TOTK_SHADER_CONTRIB` | Separate shader-contribution output file |
  | `TOTK_CONTRIB_GH` | `0` to remove automatic submission from the reminder |

- Contribution reminders may block startup before the game window exists. Do not
  submit contributions, open issues, or star repositories without authorization.
  `TOTK_CONTRIB_GH=0` does not itself disable reminders. Use isolated output and
  inspect a blocked startup before assuming the runtime has hung.
- Local test artifacts include `TOTK_build/Native Mouse Test.app` and
  `TOTK_build/native-mouse-tests/Launch Native Mouse Test.command`. The latter
  sets copied-save overrides; opening the test app directly does not guarantee
  isolation. Inspect the script and current paths before reusing it.
- Several local apps share `org.macnativegames.totk`; select by full app path for
  UI work. An app getter can launch a closed app without test overrides. Check app
  inventory to confirm termination instead of querying/relaunching it after quit.

## Build, update, and verify

Edit source in this checkout, not cached copies under `TOTK_build/totk` or bytes
inside a rewritten game module. The build tools refresh their work-tree sources.
Use the existing verified inputs and packaging checks; do not weaken hash or
instruction guards to make a build pass.

The configured local Python 3.14 test environment is `TOTK_build/venv/bin/python`.
The default system `python3.14` lacked dependencies during this session. Diagnose
environment failures separately from code failures.

```sh
TOTK_build/venv/bin/python -m unittest discover -s tests -v
TOTK_build/venv/bin/python -m unittest discover -s tests -p 'test_*mouse*.py' -v
TOTK_build/venv/bin/python -m unittest discover -s tests -p test_input_prompts.py -v
TOTK_build/venv/bin/python -m unittest discover -s tests -p test_prompt_assets.py -v
TOTK_build/venv/bin/python -m unittest discover -s tests -p test_settings_panel.py -v
```

- Full-suite baseline at `f6007835`: **1,548 tests, 2 skipped, no failures**.
  Record fresh results after relevant changes; this is historical evidence.
- GUI tests need an unlocked Mac and access to macOS frameworks. Screen lock
  caused a Tk visibility wait; restricted execution caused unrelated framework
  failures. Do not silently count interrupted, aborted, or timed-out runs as passes.
- After refreshing work sources, the runtime target is:
  `make -C TOTK_build/totk -j8 build/totk-native-v143`.
- For the main installed app, prefer the standard updater:

  ```sh
  TOTK_build/venv/bin/python update_runtime.py "Tears of the Kingdom.app" --work TOTK_build
  ```

  It validates game data, builds/packages the runtime, preserves game files,
  signs the app, and runs its self-check. Allow Metal pipeline generation to
  finish; a changed runtime can trigger several minutes of cache rebuilding.
- Verify the installed `MNGSourceCommit`, loader output, signature, and app
  `--check`. `--check` does not enter gameplay. A new bundle's first macOS scan
  may take minutes before producing output.
- Signing changes Mach-O signature bytes. Whole-file hashes can differ between
  the build runtime and signed executable despite identical code. Use the normal
  signature/loader checks; compare content sections if investigating this issue.
- The app's source label identifies its runtime build. A later docs-only commit
  does not require changing that label or rebuilding the runtime.

## Live button prompts

See [docs/live-button-prompts.md](docs/live-button-prompts.md).

- Enabled only for macOS TotK 1.4.3. Settings: **1 → Input → Button prompts**.
  Auto follows fresh device input; bindings/styles refresh without restart.
- Default Switch A is the physical Xbox **B** / PlayStation **Circle**. Showing a
  controller style must not remap input. Respect effective custom bindings,
  mouse-disabled keyboard alternatives, unbound actions, and stick-drift filtering.
- `games/totk/runtime/prompt_bindings.h` resolves bindings;
  `input_prompts.m` publishes synchronized snapshots and draws icons;
  `prompt_cache_v143.h` guards font-cache hooks.
- `games/totk/third_party/d2r_gfx/gfx/nvn/totk_prompt_icons.m` replaces prompt
  regions in renderer-owned R8 texture copies. Preserve game font memory/text
  metrics, texture residency/deferred release, scanline orientation, and revision
  invalidation of descriptor views/argument buffers.
- `prepare_prompt_hud.py` generates `build/prompt-hud.tsv` from verified local
  resources. Packaged metadata contains dimensions/hashes, not game pixels.
- Menus and normal gameplay popups are covered. Baked tutorial art and faint
  controller diagrams remain unchanged; UI mods replacing masks may need integration.
- `TOTK_PROMPT_ICONS=0` disables the live adapter for diagnosis. Static icon mods
  remain separate: their generator supports custom bindings, but needs regeneration
  and restart. Do not confuse those with live prompts.

## Direct mouse camera

See [docs/native-mouse-camera.md](docs/native-mouse-camera.md).

- `games/totk/runtime/mouse_camera_v143.h` implements the guarded macOS TotK
  1.4.3 adapter. Other versions/games retain their existing stick/gyro path.
  `TOTK_NATIVE_MOUSE_CAMERA=0` restores that path at launch.
- Keep button bindings and prompt activity tracking intact. The ability wheel
  remains stick-based; resolve its held action through effective input bindings.
- Installed hooks do not mean the current camera is supported. Ultrahand's
  held-object camera does not call these hooks. `runtime_mouse_camera_active()`
  tracks callbacks even while idle and expires after 125 ms; unhandled states
  retain mouse-driven stick input without simulated gyro. Preserve fallback and
  re-entry clearing, or grabbed objects stop moving despite successful F/E input.
- Preserve fractional movement, synchronized paired deltas/settings, stale-input
  handling, and clearing when capture or wheel routing changes. Sensitivity 50 is
  0.1 degree per AppKit point. This is direct camera input, not raw HID or a menu cursor.
- Preflight all four original instructions and the full reservation before writes.
  Native veneers use the executable tail after the first **640 bytes** reserved
  for effects/survey hooks, leaving the last **128 bytes** for VR. Prompt hooks
  use separate text padding. Recheck all reservations when adding patches.
- Guest callbacks must use `runtime_hle_wrap_named` so host work uses the host
  stack. Preserve integer/full SIMD registers, NZCV, FPCR, and FPSR, then execute
  the displaced instruction and return to its original continuation.
- Exclude Mac callback addresses from `TOTK_EXPORT_IOS`, including exports run
  on a Mac. A compile-time iOS guard alone is insufficient.
- Retain the dxnside attribution in `THIRD_PARTY_NOTICES.md` and its MIT notice
  under `games/totk/third_party/native_mouse_camera/`; packaging includes the notice.

## Gameplay diagnostics and coverage limits

- `TOTK_CONTROLS` is a bindings path, **not** the command channel. Append complete
  commands to `TOTK_INPUT_FILE` after app startup; the launcher truncates it at start.
- `mousemove <dx> <dy>` sends finite relative AppKit deltas within ±8192 through
  the main-thread mouse handler only while capture is active.
  `TOTK_NATIVE_MOUSE_LOG=1` logs consumed angles; diagnostics stay off normally.
- UI automation drags supplied absolute positions without useful relative motion
  in this session. Do not mistake that limitation for a camera failure. Use the
  diagnostic path for measured deltas and a physical mouse check for feel.
- `wshot <absolute PNG path>` captures the presented window frame. `shot` can
  include unused black portions of the backing texture. Keep captures local.
- Input-file key commands use the fixed synthetic layout documented in `input.c`;
  real keyboard events use `keyboard.txt`. Do not confuse their mappings.
- Verified in the Room of Awakening: horizontal/vertical turns, live sensitivity,
  capture release, and **F · Examine** with both features enabled. The user also
  physically tested the mouse and confirmed smooth camera turning.
- Follow-up validation used a fresh copy of the main app's latest Great Sky Island
  save near a tree log. F/E grabbed it; mouse movement failed while held before the
  activity-based fallback fix, then worked afterward. The user confirmed physical
  movement worked. Returning to the normal camera resumed direct movement (50
  points at sensitivity 70 produced 7 degrees). Main saves matched all 32 pre-test
  hashes. Full suite: 1,548 tests, 2 skipped, no failures (339 s).
- Bow aiming, Recall, the ability wheel, and object rotation still need actual
  gameplay checks. Synthetic coverage is not gameplay proof.
  No Xbox/PS5 controller or VR headset was available; iOS was not built because
  its SDK was unavailable. Do not claim those were verified.
- Scene-limited shader warm-up was used in diagnostic runs. Treat their results
  as functional validation, not performance benchmarks.
