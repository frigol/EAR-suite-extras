# reaper-adm-extension patch (ADM export recognition for Reverb / IR Reverb)

**Who needs this**: only people who want to *export* a session containing
**Reverb for EAR Suite** or **IR Reverb for EAR Suite** to BW64/ADM via
REAPER's ADM export (the official `reaper-adm-extension`, installed
alongside the upstream [EAR Production Suite](https://github.com/ebu/ear-production-suite)).
If you only use these two plugins for monitoring/mixing and never export
ADM, you don't need this.

**Why it's needed**: `reaper-adm-extension` recognizes input plugins (EAR
Object, DirectSpeakers, HOA) by exact VST3 plugin name and reads their
position/gain by raw parameter index. Reverb and IR Reverb aren't in its
list, so without this patch, exporting a session with either of them
produces `Unable to find Input plugin with instance ID ...` and the
reverb's 10 points are silently dropped from the exported file.

**What it does**: `exportaction_admsource-earvst.cpp`/`.h` are the
upstream files from `reaper-adm-extension/src/reaper_adm/` (part of the
[ear-production-suite](https://github.com/ebu/ear-production-suite) monorepo,
GPLv3, same base version this fork tracks - see the main [README](../README.md)),
with `isReverbPlugin()` extended to recognize "Reverb for EAR Suite" and
"IR Reverb for EAR Suite", and `ShiftedPluginParameter`/`matchesInputInstanceId()`
reading each plugin's 10 points' position/gain from the 80 export-mirror
parameters both plugins expose right after their 10 instance-id parameters
(see the comments in the patched `.h` file for the exact parameter layout
both plugins and this file must agree on).

## Option A: prebuilt dylib (macOS, Apple Silicon)

1. Quit REAPER.
2. Make sure the official EAR Production Suite (with `reaper-adm-extension`)
   is already installed - this replaces its dylib, it doesn't install it
   from scratch.
3. Back up the existing file, then replace it:
   ```bash
   cp ~/Library/Application\ Support/REAPER/UserPlugins/reaper_adm.dylib \
      ~/Library/Application\ Support/REAPER/UserPlugins/reaper_adm.dylib.orig
   cp reaper_adm.dylib ~/Library/Application\ Support/REAPER/UserPlugins/reaper_adm.dylib
   ```
   (The repo's top-level `install.sh` does this for you - see the main README.)
4. Restart REAPER.

To revert: copy `reaper_adm.dylib.orig` back over `reaper_adm.dylib`.

Windows/Linux/Intel Mac: no prebuilt dylib here - use Option B.

## Option B: build it yourself

1. Get the upstream `ear-production-suite` source (same version this fork's
   base version tracks - see the main README) and locate
   `reaper-adm-extension/` in it.
2. Replace `src/reaper_adm/exportaction_admsource-earvst.cpp` and `.h` with
   the two files in this folder.
3. Build `reaper-adm-extension` per upstream's own instructions (it's a
   CMake subdirectory of the main suite).
4. Copy the resulting `reaper_adm.dylib`/`.so`/`.dll` over the one in
   REAPER's `UserPlugins` folder (back up the original first), same as
   Option A step 3.

## License

These two files are a modified version of GPLv3-licensed code from
`ear-production-suite`'s `reaper-adm-extension` - see that project's own
[license](https://github.com/ebu/ear-production-suite/blob/main/LICENSE.TXT)
and the copy in this fork's own [LICENSE.TXT](../LICENSE.TXT)/[COPYRIGHT.TXT](../COPYRIGHT.TXT).
Distributed here, under the same license, as a patch against the
unmodified upstream source plus a prebuilt binary for convenience - not
affiliated with or endorsed by the BBC, EBU, or IRT.
