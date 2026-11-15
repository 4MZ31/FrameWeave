# PushFPS — notes for Claude and other agents

PushFPS is a fork of Veyra (https://github.com/Likely7/Veyra-NRVideo, GPL-3.0) focused on console
frame generation from a capture card. Owner: DosukaSOL. Repository: https://github.com/DosukaSOL/PushFPS.

These rules take precedence over `AGENTS.md`. `AGENTS.md` is Veyra upstream's own work log (Chinese,
with paths on the upstream developer's machine); read it for history and engineering contracts, but its
paths, task lists and push/release rules are not ours. Do not edit it: it changes upstream almost daily
and edits would conflict on every merge.

## Rules

1. **Keep upstream mergeable.** Internal identifiers keep Veyra's names: namespaces (`veyra::`), CMake
   targets, the `qml/Veyra` QML module, window classes, data folders, env vars (`VEYRA_*`). Only what a
   user sees says PushFPS. Product name and links live in `include/veyra/Brand.h`; use it instead of
   writing the name or URLs again.
2. **Translations are keyed by the Simplified Chinese source string.** Every user-visible string is
   written in Chinese in `qsTr(...)` / `tr(...)` and translated in `i18n/catalog.json` (zh-TW, en, ja by hand). For nl, de, es, fr, it, pt-BR, tr, ar,
   pl, ru the short interface labels are hand-written in `scripts/i18n/hand/<lang>.txt` (line order of
   `hand/en.txt`; fix a word there, never in the catalog) and longer texts are machine-translated by the
   `translate` workflow (`machine_translate.py`, which corrects wrong-sense words and keeps English for half-translated output, `merge_translations.py`, then
   `apply_hand_translations.py`), which pushes the result to the `i18n-mt` branch.
   After changing or adding a string run `python3 -B scripts/i18n/extract.py`, fill the new entries, then
   `python3 -B scripts/i18n/extract.py --check` must report 0 missing and 0 placeholder problems.
3. **QML is loaded from disk next to the exe** (`qml/Veyra`), so new QML files need a line in
   `qml/Veyra/qmldir` but no CMake change. Run the QML lint workflow (`.github/workflows/checks.yml`) on
   every QML change; there is no other QML check without a Windows build.
4. **Never commit** SDKs, NVIDIA/AMD/Intel runtimes or models, build output, or user data (exception:
   MIT-licensed shader source such as `third_party/nis`, recorded in `THIRD_PARTY_NOTICES.md`)
   (`.gitignore` covers `third_party_local/`, `runtime_local/`, `out/`, `build/`).
5. **License.** Everything stays GPL-3.0 (the Chiaki remote-play part also AGPL-3.0). Credit Veyra and
   Likely7 wherever the product is described. Copy code from other projects only with a compatible license
   and record it in `THIRD_PARTY_NOTICES.md`. `NOTICE.md` holds the GPL-3.0 modification notice, the
   section 7 additional permission for PushFPS's own code (linking with the NVIDIA/AMD/Intel SDKs) and the
   NVIDIA SDK attribution (also in the About page, required by the NVIDIA RTX SDKs License). By default the
   installer never installs NVIDIA files that are not NVIDIA-signed originals, nor NVIDIA's model runtimes on
   AMD installs; they stay available as opt-in "experimental extras" (`-ExperimentalExtras Yes` or the
   installer's question) so no feature is lost (`packaging/Install-PushFPS.ps1`; CI checks both paths). Marketing describes features, not NVIDIA
   marks: no "DLSS" in headlines, no NVIDIA logos, never imply endorsement.
6. **Be honest about verification.** Say what was built, run and tested, and on which hardware. A change
   that only passed static checks is "unverified on Windows".

## PushFPS-specific code

| What | Where |
|---|---|
| Product name, GitHub links, upstream credit | `include/veyra/Brand.h` |
| Quick setup (console and game profiles, game FPS, target FPS, AI enhancement, low latency) | `qml/Veyra/PushQuickSetup.qml`, used in `HomePage.qml` and `ProPage.qml`; preferences `pushConsole`, `pushGameFps`, `pushProfiles` (validated in `src/ui/bridge/Preferences.cpp`); tests in `tests/qml/quick/tst_push_quick_setup.qml` |
| Picture & sound editor, sound profiles, saved-recording card | `qml/Veyra/PushPicture.qml` (gamma is stored as RGB tone curve 0, not a new `ColorSettings` field, to keep the preset schema upstream-compatible), `PushSoundProfile.qml`, `PushRecordingSaved.qml`; DSP in `include/veyra/sink/SoundProfile.h` + `src/sink/SoundProfile.cpp`, applied in `WasapiAudioSink.cpp` before the recording tap; preferences `soundPreset`, `soundEq`; tests in `tests/unit/SoundProfileTests.cpp` and `tests/qml/quick/tst_push_picture_sound.qml` |
| NVIDIA Image Scaling (SR choice `engine::kVideoSrNis` = 6) | `third_party/nis` (MIT, unchanged, see `PUSHFPS_ORIGIN.md`), `shaders/NisScaler.hlsl` + `NisColor.hlsl`, `src/pipeline/NisPass.cpp`, hooks in `EnhanceGraph.cpp` (`initNis`, `createViews`, `runSr`), `GraphDescription.h` / `BackendRecovery.h` (`vendorNeutralVideoSr`); sharpness preference `nisSharpness` (live, `pipeline/NisSettings.h`); UI in `SrStageEditor.qml`; tests `tests/integration/NisGpuTests.cpp` (WARP in CI) and `tests/qml/quick/tst_push_nis.qml`. Upstream SR changes in those files may conflict: keep NIS single-pass and vendor neutral |
| ANR frame generation by Apex Compute (host from Veyra 2.1.6): never ship, download or mirror ANR files (Apex does not allow redistribution); the user downloads it from Apex and *Settings → Components and licenses → Install from download* copies it into `runtime\experimental\anr`; credits and links (Apex, Veyra) in About and the README top, as text only: Apex Compute asked us (November 2026) not to use their logo anywhere; the gate `brand::kAnrFrameGeneration` (now true) hides it if needed | `include/veyra/Brand.h` (`kAnr*` links), `Hardware.cpp`, `FrameGeneration.cpp`, `Preferences.cpp`, `pushInstallAnr` / `pushOpenLink` in `src/ui/bridge/PushFps.cpp`, `SettingsPage.qml` (`set-anr`, `about-open-anr`) |
| Optimise for my PC | `qml/Veyra/PushOptimizeDialog.qml` (dialog key `optimize`; plan, pass rules, confirm, undo); measurement window `include/veyra/engine/PushBenchmarkProbe.h`, hooked in `FrameFlowWindow.h` (`presented`, `latency`); bridge `pushLiveStats`, `pushBenchmarkBegin/End` in `src/ui/bridge/PushFps.cpp`; preference `pushBenchmark`; tests `tests/unit/PushBenchmarkTests.cpp`, `tests/qml/quick/tst_push_optimize.qml` |
| Advanced recording settings (rate control, quality level, encoder preset, keyframe interval, audio kbps, MP4/MKV) | prefs `recordAdvanced` + `recordRateControl`/`recordQuality`/`recordPreset`/`recordKeyframe`/`recordAudioKbps`/`recordContainer` (validated in `Preferences.cpp`, applied only while `recordAdvanced` is on in `Recording.cpp::recordingOptions`); `RecordingSettings` (`LiveRecorder.h`, defaults = standard encoding), `NvencLiveEncoder::Config`, `EncoderConfig` (MF); NVENC falls back to standard settings when refused; replays stay MP4; UI `set-recadvanced*` in `SettingsPage.qml`; screenshots 32-34 (`--pref`, `--scroll-to`) |
| Game presets, latency readout, Discord sharing, capture card list | `PushGamePresetsDialog.qml` (key `games`) + `PushGamePresets.js` (library, `PUSHFPS1:` codes), `pushSetExclusionRegions`; `PushLatencyBadge.qml` (pref `pushLatencyOverlay`); `PushRecordingSaved.qml` + `pushShareToDiscord` (pref `pushDiscordWebhook`, Discord URLs only); `PushCaptureCards.js` (keep in sync with the README table) and `.github/ISSUE_TEMPLATE/capture-card-report.yml`; tests `tst_push_presets.qml` |
| Adaptive multiplier, Switch presets | `EnhancementSettings::fgAdaptive` (shell pref `fgAdaptive`, left out of the configuration comparisons so toggling never rebuilds the graph); choice and hold in `include/veyra/engine/PushAdaptiveFg.h` (closest output on a log scale among `displayFgMultiplierSelectable` values up to the configured ceiling; 0.5 s gate), applied per pair in `EngineController.cpp` before the output-cap reduction (which now divides by the content rate, not the capture transport); bridge `fgAdaptive`, `pushLiveStats().fgMultiplierNow`; UI rows `pushfps-adaptive` (quick setup) and `list-fg-adaptive` (Professional), ceiling from `PushGamePresets.js` `adaptiveCeiling`; Switch/Switch 2 presets in `PushGamePresets.js` `switchGames()` (`console`, `adaptive` fields; console filter in `PushGamePresetsDialog.qml`; console id `switch` validated in `Preferences.cpp`); tests `tests/unit/PushAdaptiveFgTests.cpp`, `tst_push_presets.qml`, `tst_push_quick_setup.qml` |
| Automatic HUD detection and the box editor | detector `include/veyra/engine/PushHud.h` + `src/engine/PushHud.cpp` (pure CPU, tests `tests/unit/PushHudTests.cpp`); frame sampling `PushHudSampler` (a `PresentCapture` beside the recorders in `EngineController.h`); bridge `pushStartHudDetection` / `pushHudDetection` / `pushCancelHudDetection` in `PushFps.cpp`; dialog `PushHudDialog.qml` (key `hud`), applied with `pushSetExclusionRegions`; tests `tst_push_hud.qml`, `tests/integration/PushGpuTests.cpp` (WARP in CI) |
| Before/after comparison clips | `include/veyra/engine/RecordingCompare.h` + `.cpp` (copies on the recorder's NV12 planes: held real frame left, every frame right, divider and labels), `RecordingSettings::compare` / `compareLabels` in `LiveRecorder`, bridge `pushStartCompareClip` (labels drawn with QPainter, timer stops it); button `pushfps-compare-clip` in `PushQuickSetup.qml`; tests `PushGpuTests.cpp` |
| Help marks and the wordmark | `VHelp.qml` ("?" with a tooltip; `VRow.help` shows one beside the label), `PushWordmark.qml` (Push white, FPS `Theme.brandGreen`, Bahnschrift), used in `HomePage.qml` and About |
| Home layout, row layout, theme | `qml/Veyra/HomePage.qml`, `VRow.qml` (hint moves under wide controls; its slot does not lay out several controls, wrap them in a `RowLayout`), `Theme.qml` (`mode` default/black/light, `ink()` for overlays; pref `pushTheme`; contrast test `tst_push_appearance.qml`) |
| OBS Studio plugin | `obs-plugin/` (C, libobs headers + `.def` import libraries); installs both the OBS 33+/34 layout (`plugins\pushfps-obs\pushfps-obs.dll`) and the 28-32 one (`bin\64bit`); CI builds it and loads it into the latest OBS with only the new layout (`obs-plugin/ci/obs-load-test.ps1 -NewLayoutOnly`) |
| Streaming sources from Veyra (PS5 Remote Play, Moonlight/Sunshine, Xbox) | Veyra's code, switched on in `build-windows.yml` (`VEYRA_ENABLE_REMOTEPLAY/MOONLIGHT/XBOX=ON`): static vcpkg tree `x64-windows-static` (OpenSSL, Opus, SDL3 3.4.14, libdatachannel with `scripts/xbox/vcpkg-overlay`, ...), chiaki-ng at `CHIAKI_COMMIT` with `scripts/remoteplay/patches` (verified by `verify-chiaki-stage.py`), moonlight-common-c at `MOONLIGHT_COMMIT` (`verify-moonlight-stage.py`); the release attaches `PushFPS-<version>-streaming-source.zip` (AGPL/GPL/MPL corresponding source). Keep the pins in step with Veyra's `dependency-lock.json` files when merging upstream |
| Program name `PushFPS.exe` | `CMakeLists.txt` (`OUTPUT_NAME` on `veyra_qml_ui`) |
| Logo and icon | `qml/Veyra/logo.png`, `assets/veyra.ico` (PushFPS artwork, upstream file name), `assets/pushfps-*.png`; the icon's chevrons are traced from the full logo and every icon file (exe icon, app logo, website favicons) is generated by `scripts/brand/make_icon.py`: change the shape there, never by hand |
| Plug and play, 120 Hz tip, total-delay guide, always on top | quick setup rows `pushfps-auto-connect` (Veyra's `autoResume`) and `pushfps-auto-fullscreen` (pref `pushAutoFullscreen`, applied in `Main.qml` `onPushLiveKindChanged`); `hz120Tip` in `PushQuickSetup.qml`; `PushLatencyGuideDialog.qml` (key `latencyguide`); `pushSetAlwaysOnTop` in `PushFps.cpp` (SetWindowPos, never Qt's WindowStaysOnTopHint, which can recreate the native window) with pref `pushAlwaysOnTop`; tests `tst_push_plug_and_play.qml` |
| Sunset City look (intro, home logo, startup music) | PushFPS's own: `PushIntro.qml` (3.26 s, timed to the music's beats 0.64/0.76/1.12 s, glides into `HomePage.wordmarkItem`; `--intro-at <ms>` holds a frame for screenshots), sunset logo on `HomePage.qml` (pref `pushSunset`), prefs `introAnimation` / `introMusic` (`Main.qml` `brandIntro` / `musicOn`, never in test runs), switches `set-sunset` / `set-intro` / `set-intro-music` in `SettingsPage.qml`; art drawn by `scripts/brand/make_sunset.py` (change it there; "Sunset City", never GTA or Rockstar names or art); tests `tst_push_intro.qml`. The music is Veyra's `media/startup-music.mp3`, played from Veyra's installed package only: never copy it into the repo, the release zips or a Steam depot (its notice limits permission to Veyra packages). Veyra's own dusk palette and `DuskIntro.qml` stay unused: `Theme.qml` keeps the palette API fixed to "classic" and the palette row stays hidden when merging upstream |
| Steam store kit, code signing | `packaging/steam/` (store copy with Veyra/Likely7 and Apex credits, art by `scripts/brand/make_steam.py`; Steamworks steps need the owner's account); `packaging/signing/README.md` (SignPath Foundation is the only route that keeps the owner's name off the certificate; current blockers; never add a "Code signing policy" claim before approval) |
| Website (https://dosukasol.github.io/PushFPS/) | `website/` (static HTML/CSS/JS, self-hosted Barlow fonts, no trackers; the only request is GitHub's release API for the newest download link), published by `.github/workflows/pages.yml` on pushes to `website/**`; three looks in `html[data-theme]` (`sunset` default with the canvas sky `startSky` in `app.js`, `classic`, `light`), chosen in the nav and kept in localStorage, applied before first paint by the `<head>` script; screenshots in `website/assets/shots/*.webp` are converted from `assets/screenshots` |

## Building and releasing

- After any QML change, `tests/qml/quick` runs in CI (`tst_push_*.qml` strictly, the Veyra suite as a
  report) and the install test opens every main page and fails on `[ERROR] [qml]`. The `screenshots` workflow
  (`-f branch=<branch>`) photographs the real app and writes `qml-report.txt`.
- `.github/workflows/build-windows.yml` is the build recipe (pinned, login-free dependencies). It builds
  `PushFPS.exe`, runs the GPU-free unit tests, checks the exe's Qt/FFmpeg imports against Veyra's runtime
  package, packages `packaging/make-release.ps1`, then installs with `packaging/Install-PushFPS.ps1` and
  starts the program; QML errors at startup fail the build. FFmpeg (~25 min) and the static streaming libraries (~30 min) are kept in a vcpkg binary cache saved by every run (`vcpkg-bin-<commit>-v2-<run>`), so a run with an unchanged compiler restores them in seconds.
- Releases: push a tag `vX.Y.Z` (stable numbers only: the in-app update checker ignores other tags). The
  workflow publishes it as a GitHub **pre-release**; mark it as the latest release only after a person has
  tested it with a real capture card and GPU, or when the owner asks for a full release (dispatch with
  `-f release_tag=vX.Y.Z -f full_release=true`).
  The workflow token may only create the tag when the built commit's workflow files match `main` at release
  time: don't push workflow changes while a release build runs. If the release step still fails (or saves a
  draft), publish the same files without rebuilding: dispatch `release-from-run.yml` with the run's ID
  (`scripts/release/publish.sh` writes the notes for both). What's new goes in `packaging/release-notes/vX.Y.Z.md`. Builds without a tag are versioned `2.1.5-pushfps.<run>`.
- The upstream runtime package version is `UPSTREAM_TAG` in the workflow; its SHA-256 values are pinned into
  the installer at build time from Veyra's own `SHA256SUMS.txt`. Moving to a newer Veyra release means
  merging upstream and changing `UPSTREAM_TAG` together.
- Release builds have no NVIDIA Optical Flow SDK (login-only): `src/ngx/NvOfSessionUnavailable.cpp` makes
  NVOF report unavailable and the app uses AMD FidelityFX optical flow.

## Syncing with Veyra

```
git remote add upstream https://github.com/Likely7/Veyra-NRVideo.git   # once
git fetch upstream main
git merge upstream/main
```

Expected conflicts: `README.md`, `README_CN.md`, `README_EN.md` (keep ours), `i18n/catalog.json` (merge,
then re-run `extract.py` and `--check`), and any file listed in the table above.
