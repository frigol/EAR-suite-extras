# EAR Suite Extras

Unofficial plugins extending the [EBU ADM Renderer Production Suite](https://github.com/ebu/ear-production-suite) (GPLv3). Built on top of its shared library code, not a patch to the upstream repository.

**This is an independent, unofficial fork. It is not affiliated with, endorsed by, or supported by the BBC, EBU, or IRT.** See [COPYRIGHT.TXT](COPYRIGHT.TXT) and [LICENSE.TXT](LICENSE.TXT).

**Version 1.0.1** of this repo — based on EAR Production Suite **1.2.2**. This project has its own version number and release history, independent of upstream's: check the [Releases page](../../releases) for updates, not upstream's.

## What's here

- **Object OSC for EAR Suite** — the EAR Object plugin with OSC remote control added: an external system (e.g. [ControlGRIS2](https://github.com/GRIS-UdeM/ControlGris)) can drive an object's position and extent live. Several instances can share one UDP port, each identified by its own source index.
- **Custom Monitoring for EAR Suite** — the EAR Monitoring plugin, but the loudspeaker layout is loaded at runtime from a plain text file instead of chosen from a fixed list of standard layouts, so it can decode to any loudspeaker rig, including subwoofers at arbitrary positions.

Both are VST3 only (macOS Apple Silicon prebuilt; other platforms need a source build — see below).

## Installing the prebuilt plugins

1. Quit your DAW if it's running.
2. Copy both folders from [`Compiled Plugins/VST3/`](Compiled%20Plugins/VST3/) into your VST3 plugins folder:
   - macOS: `~/Library/Audio/Plug-Ins/VST3/`
   - Windows: `C:\Program Files\Common Files\VST3\`
3. Restart your DAW and rescan plugins if it doesn't pick them up automatically.

The prebuilt `.vst3` bundles in this repo are macOS (Apple Silicon, arm64) only. For Windows, Linux, or an Intel Mac, build from source (below).

---

## Custom Monitoring: the loudspeaker layout file

Loaded via the **Load layout...** button in the plugin. One speaker per line:

```
name azimuth elevation [radius] [gain] [lfe]
```

- Fields separated by whitespace and/or commas.
- Lines starting with `#` (after leading whitespace) and blank lines are ignored — use them for comments.
- **name**: the channel's label, shown in the plugin's meter grid. Free text, except for one reserved case (see T+000, below).
- **azimuth, elevation**: degrees. Azimuth 0° = front, positive = anticlockwise (left) — matching the ITU-R BS.2051 / EAR convention. Elevation 0° = horizontal, +90° = directly overhead, -90° = directly below.
- **radius**: metres from the listening position. Optional, defaults to `1.0`.
- **gain**: linear trim applied to this output *after* EAR's own gain calculation — use it to balance levels across speakers at different distances/sensitivities. Optional, defaults to `1.0` (no change). This is a plain multiplier, not dB (e.g. `0.5` ≈ -6 dB, `2.0` ≈ +6 dB).
- **lfe**: `0`/`1`, `true`/`false`, or `yes`/`no`. Optional, defaults to `0`. A subwoofer/LFE channel is excluded from Object panning (objects never pan to it directly, same as any standard LFE channel in EAR) but still receives signal for DirectSpeakers or HOA content that explicitly targets that channel.

### Example: 5.1 with a subwoofer

```
# 5.1 example
M+030   30    0
M-030  -30    0
M+000    0    0
LFE      0    0   1.0   1.0   1
M+110  110    0
M-110 -110    0
```

### Two things that will crash the plugin if you get them wrong

- **A speaker placed exactly at the zenith (elevation 90°) must be named exactly `T+000`** (case-sensitive). The underlying rendering library always adds its own virtual speaker at that exact position unless the layout already has a real channel with that specific name; without it, the real and virtual points coincide exactly and the geometry calculation throws an error (visible as "collinear points in convex hull" if you're building from source and see the exception). Any other name at elevation 90° will crash on load.
- **No speaker can sit at exactly the nadir (elevation -90°)** — the library always adds its own virtual speaker there regardless of name, so a real one at exactly -90° always collides the same way, with no naming workaround. If you need a speaker close to directly below, nudge it a fraction of a degree off the exact pole (e.g. -89.9°).

If a file fails to load, the plugin shows an error dialog with the problem and line number rather than crashing the host — but the two cases above are a known limitation in the underlying geometry library, not something this plugin can detect in advance beyond refusing the exact pole position.

---

## Object OSC for EAR Suite: remote control

In the plugin, tick **OSC remote control** and choose a **Protocol**:

### ControlGRIS2 (recommended default)

Reverse-engineered from a live capture of ControlGRIS2's actual OSC output (not the `/spat/serv` scheme documented in SpatGRIS's README, which ControlGRIS2's own "Controllers" tab does not send).

- **Port**: must match the port ControlGRIS2 sends to (its "Send on port" setting, Configuration → Settings tab).
- **Src #**: must match ControlGRIS2's own "OSC plugin ID" for the source you want this instance to follow.
- Several Object OSC instances can share the *same* port, each with a different Src #, driven by different ControlGRIS2 instances or sources — each instance filters incoming messages by its own Src # and ignores the rest. (Internally, the plugin shares one UDP socket per port across all its own instances in the host process — you don't need a separate port per instance.)
- Controls: Azimuth and Elevation (from ControlGRIS2's position pad), and Width/Height (from ControlGRIS2's Azimuth Span / Elevation Span, with Size automatically unlinked so the two move independently).
- ControlGRIS2's DOME mode has no distance control, so Distance is left fixed at 1.0 — not driven by OSC in this protocol.

### Custom (`/object/*`)

This plugin's own scheme, one message per parameter, a single float32 argument in the same units as the matching automation parameter (not normalised 0–1):

| Address | Range |
|---|---|
| `/object/azimuth` | -180..180 |
| `/object/elevation` | -90..90 |
| `/object/distance` | 0..1 |
| `/object/gain` | -100..6 (dB) |
| `/object/size` | 0..1 |
| `/object/width` | 0..1 |
| `/object/height` | 0..1 |
| `/object/depth` | 0..1 |
| `/object/diffuse` | 0..1 |

The plugin's OSC debug line (top of the window) shows the last message it received, whether or not it matched — the fastest way to check if anything is arriving at all, and whether the address/Src # match what you expect.

---

## Building from source

Requires a C++20 compiler, CMake ≥ 3.14, and (on macOS) Xcode command line tools. The build fetches its own vcpkg-managed dependencies and git submodules — no need to install Boost/JUCE/etc. yourself.

```bash
git clone --recursive https://github.com/frigol/ear-suite-extras.git
cd ear-suite-extras
cmake --preset macos-default-arm64   # or macos-default (x64), see CMakePresets.json
cmake --build build/macos-default-arm64 --target ear_custom_monitoring_VST3 object_osc_VST3
```

Built plugins land in `build/<preset>/ear-production-suite-plugins/VST3/`. Copy them to your VST3 folder as above.

If you cloned without `--recursive`, run `git submodule update --init --recursive` afterwards.

This repo is a trimmed fork: it carries only the shared library code and CMake plumbing needed to build these two plugins, not the full upstream EAR Production Suite (other plugins, the REAPER ADM export extension, installers). For the complete suite, see the [upstream repository](https://github.com/ebu/ear-production-suite).

## License

GPLv3, same as upstream — see [LICENSE.TXT](LICENSE.TXT) and [COPYRIGHT.TXT](COPYRIGHT.TXT). Source code for both plugins is in this repository under `ear-production-suite-plugins/plugins/`.
