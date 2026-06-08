# Preparing GTUltra Songs for 1raster Conversion

The 1raster player is intentionally tiny. It can produce useful SID music, but it is not a general GoatTracker player. To get near-lossless conversion, compose for the 1raster model from the start.

## Target the Player Shape

Use only the first SID: channels 1, 2, and 3. The converter ignores GTUltra channels 4-6 and reports a warning when they contain data.

Treat channels 1 and 2 as note-only voices. The 1raster player writes pitch for these voices and keeps them legato. Leave empty rows between notes when you need a staccato feel.

Use channel 3 for the material that needs waveform changes, arps, percussion, or SID register tricks. In 1raster, channel 3 pattern values call a program-table entry, so it is the only channel with practical per-row sound programming.

## Stay Within Format Limits

For ORM compatibility:

- Use 32-row patterns.
- Keep each track to 8 or fewer unique converted patterns.
- Keep each orderlist to 64 positions.

For ORB compatibility:

- Use 48-row patterns or shorter material that chunks cleanly into 48-row blocks.
- Keep each track to 32 or fewer unique converted patterns.
- Keep each orderlist to 64 positions.

ORB is the safer target for real songs. ORM is best for small sketches and very compact tunes.

## Compose With Conversion-Friendly Instruments

Prefer simple, stable instruments:

- A single waveform or a short waveform/arpeggio table.
- Clear ADSR values.
- Simple pulse width defaults.
- Simple filter cutoff/control changes.

Avoid relying on these if you need near-lossless conversion:

- Long wavetable programs.
- Complex pulse and filter table automation.
- Wavetable commands in the `$f0..$fe` range.
- Per-channel tempo tricks.
- Dense effect-only rows on channels 1 and 2.
- 6-channel arrangements.

The converter can translate simple channel-3 waveform arps, ADSR changes, waveform register writes, filter cutoff/control, volume, and tempo writes. More complex GTUltra effects are reported as warnings.

## Suggested Workflow

1. Start the tune in 3-channel mode.
2. Keep the main musical lines on channels 1 and 2.
3. Build drums, arps, and filter gestures on channel 3.
4. Reuse patterns and instruments aggressively.
5. Run analysis before exporting:

```sh
tools/gtultra2raster/gtultra2raster song.sng --analyze
```

6. Fix warnings that matter musically.
7. Convert:

```sh
tools/gtultra2raster/gtultra2raster song.sng -o song-1r
```

This writes binary `.orm` when the tune fits the compact target and binary `.orb` when it fits the broader target. Use `--format orm`, `--format orb`, or `--format both` to select which binary files are written.

To also emit Kick Assembler source:

```sh
tools/gtultra2raster/gtultra2raster song.sng -o song-1r --asm
```

`--asm` writes a standalone player+data source with `INIT`, `PLAY`, and `RASTER_TUNE` labels. The source is a byte-exact Kick Assembler representation of the selected 1raster image, so it can be included in a demo build without requiring 64tass. Use `--asm-mode data` or `--asm-data` when you only want the tune data at `RASTER_TUNE` and will provide your own 1raster player build. If both `.orm` and `.orb` are generated, ASM defaults to ORB; use `--asm-format orm` or `--asm-format orb` to select the source target.

## Practical Checklist

Before final conversion, check:

- Channels 4-6 are empty.
- The song fits the target pattern and orderlist limits.
- Channel 3 program-table usage is below the reported limit.
- Unsupported effect warnings are expected and acceptable.
- GT orderlist loops restart at position 0 cleanly. 1raster playback wraps its 64-position sequence to the beginning, so nonzero GoatTracker restart positions are not preserved exactly without external wrapper code.
- The generated `.orb` loads in 1rasterb tracker.
- The generated Kick Assembler source assembles if you plan to use it in a demo build.
