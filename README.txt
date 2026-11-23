NegativeDelay16 — 16-pair stereo negative delay for REAPER
===========================================================

A lightweight native VST2 effect that applies per-stereo-pair time
adjustment (negative or positive) on a single multi-channel track,
replacing hundreds of individual delay plugin instances.

INSTALL
-------
Copy NegativeDelay16.dll to your VST folder, e.g.:
    C:\Program Files (x86)\VstPlugins
In REAPER: FX browser -> "Scan for new plugins".

USAGE
-----
1. Insert "NegativeDelay16" on a track or container with at least 32
   audio channels (routing -> channel count).
2. Route each stereo pair into the track (e.g. from a multi-output
   instrument such as SINE Player).
3. Set each pair's delay in milliseconds with the P1..P16 sliders.
   Range is -500 to +100 ms; 0 by default.
4. "Merge" (on by default) sums all 16 delayed pairs down to output
   pair 1/2 and silences the remaining outputs.

HOW NEGATIVE DELAY WORKS
-----------------------
Native VST plugins cannot report negative latency, so the plugin
reports a positive PDC equal to the most negative value in use and
buffers each pair by the difference. REAPER's delay compensation
shifts everything else later, so each pair lands early by exactly
its slider value.

IMPORTANT: PDC is only re-applied by REAPER when playback is fully
stopped and restarted. Set values while stopped (set-and-forget
workflow).

PRESETS
-------
The preset dropdown includes instrument configurations baked in by
the author; selecting one sets all 16 pair values and names the
sliders after the articulations. Program 0 "Init" zeroes everything.
You can save your own REAPER presets via the FX chain Presets menu;
they store all 17 parameter values.

TIPS
----
- FX chain Options -> Chain PDC mode should be "Auto".
- The FX chain latency readout shows the current baseline PDC
  (equal to the most negative pair value in use).
