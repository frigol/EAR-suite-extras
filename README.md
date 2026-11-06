# EAR Suite Extras

Unofficial plugins extending the [EBU ADM Renderer Production Suite](https://github.com/ebu/ear-production-suite) (GPLv3). Built on top of its shared library code, not a patch to the upstream repository.

**This is an independent, unofficial fork. It is not affiliated with, endorsed by, or supported by the BBC, EBU, or IRT.** See [COPYRIGHT.TXT](COPYRIGHT.TXT) and [LICENSE.TXT](LICENSE.TXT).

**Version 1.3.0** of this repo — based on EAR Production Suite **1.2.2**. This project has its own version number and release history, independent of upstream's: check the [Releases page](../../releases) for updates, not upstream's.

## What's here

All plugin names below are short DAW-visible names ("LF ..."); every one of them is for **EAR Production Suite** and needs its shared library/EAR Scene already running in the session.

- **LF Object OSC** — the EAR Object plugin with OSC remote control added: an external system (e.g. [ControlGRIS2](https://github.com/GRIS-UdeM/ControlGris)) can drive an object's position and extent live. Several instances can share one UDP port, each identified by its own source index.
- **LF Object OSC Stereo** — a single plugin instance driving a *linked pair* of Objects (two independent Scene connections) from one set of controls: a shared centre azimuth/elevation/distance, a Pair Distance (angular separation between the two objects, split evenly around the centre) and a Pair Rotation (the separation axis's roll - 0° spreads the pair horizontally, i.e. classic L/R stereo; 90° spreads it vertically). Gain/Width/Height/Depth/Diffuse apply identically to both objects. Has the same OSC remote control as LF Object OSC, driving the shared centre. Saves placing and keeping two ordinary Object plugins in sync by hand for a conventional stereo pair.
- **LF Custom Monitoring** — the EAR Monitoring plugin, but the loudspeaker layout is loaded at runtime from a plain text file (or an IEM AllRADecoder `.json` export) instead of chosen from a fixed list of standard layouts, so it can decode to any loudspeaker rig, including subwoofers at arbitrary positions and rooms with unequal speaker distances (per-speaker gain/delay compensation).
- **LF Reverb** — a direction-weighted spatial reverb: reports 10 points (the same "4+6+0" hexagon+top-4 layout as BS.2051's own mid/top rings) to EAR Scene as independent Objects, each panned from the active source(s) feeding it, with early reflections plus a shared algorithmic diffuse tail (switchable between a custom FDN and an MVerb plate engine). Needs EAR Scene (from the official EAR Production Suite) already running in the session.
- **LF IR Reverb** — the same 10-point architecture as LF Reverb, but the diffuse tail is a convolution against a loaded impulse response instead of an algorithmic engine. Accepts mono, stereo, AmbiX (any order), or a 9/10-channel discrete array IR (PCMA-3D-style layout); each of the 10 output points has a full Object-style parameter set (position, gain, width/height/depth, diffuse) and can be adjusted independently afterwards.
- **LF Multi-Encoder** (source build only, see below) — up to 24 independently-activatable points, each a full Object-style encoder reading one of its own input channels; several LF Reverb/LF IR Reverb instances' matching points can be summed into one shared instance instead of each registering its own Objects with Scene.
- **LF LFE Send** (source build only, see below) — like DirectSpeakers, but exposes only the LFE channel, so a track that only needs to feed the subwoofer doesn't have to carry a full speaker-layout pack format.

Reverb/Multi-Encoder plugins also need **EAR Scene** running in the session to hear anything - they register their points with it exactly like a normal EAR Object plugin, they don't output audio on their own track.

Five plugins (Object OSC, Object OSC Stereo, Custom Monitoring, Reverb, IR Reverb) are VST3 prebuilt (macOS Apple Silicon); LF Multi-Encoder and LF LFE Send are source-build only for now (see below) - all platforms other than macOS Apple Silicon also need a source build.

## Installing the prebuilt plugins

**macOS**: run the installer from the repo root:

```bash
./install.sh
```

This copies the prebuilt `.vst3` bundles into `~/Library/Audio/Plug-Ins/VST3/`, and - if it detects REAPER's official `reaper-adm-extension` already installed - patches it so this fork's plugins export correctly to ADM (see [reaper-adm-extension-patch/README.md](reaper-adm-extension-patch/README.md); it's safe to re-run, and it backs up the original file first).

Or by hand, on any platform:

1. Quit your DAW if it's running.
2. Copy the folders you want from [`Compiled Plugins/VST3/`](Compiled%20Plugins/VST3/) into your VST3 plugins folder:
   - macOS: `~/Library/Audio/Plug-Ins/VST3/`
   - Windows: `C:\Program Files\Common Files\VST3\`
3. Restart your DAW and rescan plugins if it doesn't pick them up automatically.
4. If you use REAPER's ADM export with LF Reverb or LF IR Reverb, also apply the patch described in [reaper-adm-extension-patch/README.md](reaper-adm-extension-patch/README.md) - without it, export silently drops these two plugins' points.

The prebuilt `.vst3` bundles in this repo are macOS (Apple Silicon, arm64) only. For Windows, Linux, or an Intel Mac, build from source (below).

---

## LF Custom Monitoring: the loudspeaker layout file

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

## LF Object OSC: remote control

In the plugin, tick **OSC remote control** and choose a **Protocol**:

### ControlGRIS2 (recommended default)

Reverse-engineered from a live capture of ControlGRIS2's actual OSC output (not the `/spat/serv` scheme documented in SpatGRIS's README, which ControlGRIS2's own "Controllers" tab does not send).

- **Port**: must match the port ControlGRIS2 sends to (its "Send on port" setting, Configuration → Settings tab).
- **Src #**: must match ControlGRIS2's own "OSC plugin ID" for the source you want this instance to follow.
- Several LF Object OSC instances can share the *same* port, each with a different Src #, driven by different ControlGRIS2 instances or sources — each instance filters incoming messages by its own Src # and ignores the rest. (Internally, the plugin shares one UDP socket per port across all its own instances in the host process — you don't need a separate port per instance.)
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
cmake --build build/macos-default-arm64 --target ear_custom_monitoring_VST3 object_osc_VST3 reverb_VST3 ir_reverb_VST3
```

Built plugins land in `build/<preset>/ear-production-suite-plugins/VST3/`. Copy them to your VST3 folder as above.

If you cloned without `--recursive`, run `git submodule update --init --recursive` afterwards.

This repo is a trimmed fork: it carries only the shared library code and CMake plumbing needed to build these plugins, not the full upstream EAR Production Suite (other plugins, the REAPER ADM export extension, installers). For the complete suite, see the [upstream repository](https://github.com/ebu/ear-production-suite). If you need the REAPER ADM export extension to recognize this fork's plugins, see [reaper-adm-extension-patch/README.md](reaper-adm-extension-patch/README.md).

## License

GPLv3, same as upstream — see [LICENSE.TXT](LICENSE.TXT) and [COPYRIGHT.TXT](COPYRIGHT.TXT). Source code for all plugins is in this repository under `ear-production-suite-plugins/plugins/`. LF Reverb's MVerb tail engine is third-party code under its own license - see [`ear-production-suite-plugins/plugins/reverb/third_party/MVERB_LICENSE.txt`](ear-production-suite-plugins/plugins/reverb/third_party/MVERB_LICENSE.txt). The `reaper-adm-extension` patch (needed only for ADM export) is distributed separately under [reaper-adm-extension-patch/](reaper-adm-extension-patch/) — see its own README for details and license notes.
