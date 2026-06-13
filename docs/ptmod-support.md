# ProTracker MOD Support

GTUltra can load a 31-sample, 4-channel ProTracker `.mod` file alongside the
current GT song. MOD playback is synchronized with editor playback and mixed
into the normal host audio path through the vendored libxmp player. MOD preview
is rendered as centered mono: all four ProTracker channels are mixed together
and added equally to the left and right GTUltra output channels.

## Loading

Use `Ctrl+Shift+F9` and select a `.mod` file. The loader accepts standard
4-channel ProTracker module signatures:

- `M.K.`
- `M!K!`
- `4CHN`
- `FLT4`

Loading is intentionally limited to 31-sample, 4-channel MODs. If another
format is selected, the current MOD remains loaded and the editor reports the
error. When the current MOD has unsaved edits, GTUltra asks whether to save or
discard them before replacing it.

The loaded path is stored in the `.sng` file as a GTUltra extension chunk, so
reopening the song restores the MOD when the file still exists at that path.

## Editor

Use `Ctrl+Shift+F8` to open the dedicated MOD editor. The main grid shows one
64-row ProTracker pattern at a time for the selected order position. Each
channel cell shows the usual ProTracker pattern fields:

- note name derived from the stored period
- sample number
- effect command
- effect parameter

Arrow keys move through rows and fields. On the note field, the normal GTUltra
QWERTY note keys enter ProTracker notes in the current octave and write the
currently selected MOD sample number with the note. Hex keys edit the sample,
effect, and effect-parameter fields. Effect parameters are clamped for
ProTracker-specific commands such as `Bxx`, `Cxx`, and `Dxx`, and the side
panel shows a short helper for the selected effect. Press `Enter` or `Space` on
the effect or parameter field to open the effect editor. It lists every standard
`0xx`-`Fxx` command plus all `E0x`-`EFx` subcommands, shows the parameter
meaning, and lets left/right adjust the parameter before applying it. `Delete`
clears the selected field. MOD pattern, order, title, and sample edits are
captured by MOD-aware `Ctrl+Z` undo and `Ctrl+Y` or `Ctrl+Shift+Z` redo.

Pattern editing tools:

- `Ctrl+A` marks the full current pattern.
- `Ctrl+B` starts a block mark; press it again at another cell to extend the
  block.
- `Ctrl+C`, `Ctrl+X`, and `Ctrl+V` copy, cut, and paste the marked block.
- `Ctrl+T` transposes the marked block up one semitone; `Ctrl+Shift+T`
  transposes it down.
- `Ctrl+Insert` inserts a row in the current pattern.
- `Ctrl+Delete` deletes the current pattern row.
- `Ctrl+P` clones the current pattern into a new pattern slot and assigns the
  current order to the clone.
- `Ctrl+Shift+P` clears the current pattern.

`Ctrl+Up` and `Ctrl+Down` move to the previous or next order position, so
repeated order entries show the pattern they reference. `Ctrl+F` toggles
follow/scroll mode. The same toggle is exposed as a side-panel `Follow` button
and as the forward transport icon while the MOD editor is active. When follow is
on during playback, the MOD editor tracks and scrolls to the currently playing
order, pattern, and row.

The right-side panel starts with the order editor:

- `Title` edits the 20-character ProTracker module title.
- `Length` changes the active song length.
- `Restart` changes the ProTracker restart order.
- `Follow` toggles playback follow/scroll.
- The `ORDER` list shows individual order entries and their pattern numbers.
- Left/right on an order entry changes its pattern number.
- Hex keys on an order entry type the pattern number directly.
- `Insert` inserts an order entry.
- `Delete` removes an order entry.
- `Enter` or `Space` jumps the pattern grid to the highlighted order.

`Ctrl+N` creates a blank 4-channel MOD, `Ctrl+R` reloads the current MOD path,
and `Ctrl+U` unloads the MOD. These actions also use the dirty-save prompt.

## Runtime Mixer

Below the order list, the right-side panel controls host preview behavior:

- preview on/off
- master MOD mix volume
- start delay in GT frames
- per-channel volume and mute

These runtime settings are persisted in the `.sng` extension chunk.

`Ctrl+M` toggles the PT-style scopes/meters view in the MOD side panel. The
scope view shows libxmp-reported per-channel volume, pan, sample position, and
mute state. It is a monitor view only; GTUltra still renders the MOD preview as
centered mono.

## Sample Manager

The MOD editor side panel includes the selected ProTracker sample header:

- sample index
- read-only sample name from the loaded MOD or imported sample filename
- finetune
- volume and length
- loop start
- loop length
- a compact waveform preview
- sample file actions

Left/right on these rows changes the selected sample or edits the numeric
sample header fields. Sample names are not edited directly in GTUltraPro; when
a sample is imported or replaced, its slot name is derived from the imported
filename.

`Enter` or `Space` on the sample rows opens the full sample editor popup.
`Ctrl+W` opens the same popup directly. It shows a larger waveform with crop
start/end and loop start/end markers. Use up/down to choose the marker or rate
field, left/right to move the value, and mouse-drag inside the waveform to move
the selected marker. `A` auditions the selected sample, `C` crops the selected
range, `T` trims leading and trailing near-silence, `L` applies the loop range,
and `R` resamples using the source/target rate fields.

`Ctrl+I` imports or replaces the selected sample. After selecting the source
file, an import/options dialog lets you choose raw signedness/bit depth,
channel count, normalize on/off, and optional source-to-target-rate resampling.
PCM 8-bit and 16-bit WAV files are converted to mono signed 8-bit sample data.
Uncompressed IFF/8SVX files are accepted as signed 8-bit sample data. Odd-length
samples are padded by one zero byte because MOD sample lengths are stored as
words. `Ctrl+E` exports the selected sample as raw signed 8-bit data, or as an
8-bit mono WAV when the selected export filename ends in `.wav`. `Ctrl+D`
deletes the selected sample after confirmation.

## Saving

When the MOD editor is active, `F11` saves the current `.mod` file. If the MOD
has no path yet, `F11` opens Save As. `Shift+F11` always opens Save As. Pattern
cell edits, order edits, title edits, sample header edits, and sample data
changes are written back in standard ProTracker layout with 31 sample headers,
128 order slots, 64-row patterns, and signed 8-bit sample data.

## Playback Sync

The GT song remains the master transport. Starting, stopping, playing from a SID
song position, and GT loop resets seek MOD preview to the same numeric order and
row, clamped to the loaded MOD length. Between those transport events, libxmp
advances the MOD normally so ProTracker pattern breaks and order progression are
preserved. The MOD preview is not a C64 replay export; it is host audio mixed by
libxmp.

## libxmp Playback

GTUltra vendors libxmp in `3rdparty/libxmp` and builds MOD audio preview by
default:

```sh
./build-macos.sh
```

Use `GTULTRA_LIBXMP=0` to build without the vendored player:

```sh
GTULTRA_LIBXMP=0 ./build-macos.sh
```

When GTUltra is built with `GTULTRA_LIBXMP=0`, MOD loading/editing/saving
remain available but audio preview is disabled.

## Export

GT packer/relocator exports still export the GT song. If a MOD is loaded,
GTUltra writes a sidecar `.mod.map` with the MOD path and GT player range. This
does not emit a C64 combined ProTracker MOD replay.
