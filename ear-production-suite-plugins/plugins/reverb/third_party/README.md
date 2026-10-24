# Third-party code

## MVerb.h

Vendored unmodified from https://github.com/martineastwood/mverb (commit at
time of vendoring: `master` branch, fetched for this project). Copyright (c)
2010 Martin Eastwood, licensed under the **GNU General Public License v3**
(see `MVERB_LICENSE.txt`), which is compatible with this suite's own GPLv3
licensing.

Used as an alternative, selectable diffuse-tail engine (a Dattorro-style
plate reverb) alongside this plugin's own hand-written FeedbackDelayNetwork
- see `reverb_tail_engine.hpp`/`.cpp` for the adapter that lets either one
drive the 10 output points' diffuse tail.
