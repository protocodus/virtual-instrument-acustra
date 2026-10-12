# Acustra user guide

Acustra is an acoustic steel-string guitar for VST®3 and Audio Unit hosts,
with a standalone app. Every note is synthesised in real time from six
physical string models, a measured bridge and a measurement-derived body;
there are no recorded samples. This guide covers version 1.3.2.

## Install

Each package is named `Acustra-<version>-build.<number>-<platform>-<arch>`.
Next to it, `-SHA256SUMS.txt` lists the package's SHA-256 checksums and
`.manifest.json` records the build it came from.

### macOS (VST3, Audio Unit, standalone)

The architecture in the file name says where it runs: `arm64` on Apple
silicon, `universal` on Apple silicon and Intel. Acustra is built for
macOS 11 or later.

- **Installer (`.pkg`)**: open it and follow the steps. It installs
  `Acustra.vst3` to `/Library/Audio/Plug-Ins/VST3/`,
  `Acustra.component` to `/Library/Audio/Plug-Ins/Components/`,
  `Acustra.app` to `/Applications/`, and this guide with the licence
  files to `/Library/Application Support/Acustra/Documentation/`.
- **Archive (`.zip`)**: it holds the same folders. Copy each bundle to
  the folder named above.

macOS checks a downloaded installer when it is opened. Bundles unpacked
from a downloaded archive keep the download's quarantine flag, so macOS
checks each of them too when it is first opened or loaded; bundles the
installer puts in place carry no such flag. A build that Apple has not
notarized fails the check: macOS reports that it cannot verify the
developer, and a host may refuse to load the plug-in.

### Windows (VST3, standalone; 64-bit)

Unzip the `.zip`. Copy the folder `VST3/Acustra.vst3` into
`C:\Program Files\Common Files\VST3\`. The standalone app is
`Standalone/Acustra.exe` and runs from any folder. Windows SmartScreen
may stop a downloaded app that is not Authenticode-signed before it runs.

### Linux (VST3, standalone; x86-64)

Unpack the `.tar.gz`. Copy the folder `VST3/Acustra.vst3` into `~/.vst3/`
(for your user) or `/usr/lib/vst3/` (for every user). The standalone app
is `Standalone/Acustra`. Both need the system's FreeType and fontconfig
libraries, and the app needs ALSA.

### Uninstall

Delete `Acustra.vst3`, `Acustra.component` and `Acustra.app` from the
folders above (and, on macOS, `/Library/Application Support/Acustra/`).

## First sound

1. In your host, rescan plug-ins if it does not find Acustra, then put
   Acustra on an instrument track and arm the track for MIDI.
2. Play anything from E2 to C6 (MIDI 40-84) in standard tuning. Notes a
   guitar in the chosen tuning cannot fret are ignored. This guide calls
   middle C (MIDI 60) C4; the on-screen keyboard's labels and the panel's
   legend count it as C3, so their octave numbers are one lower.
3. With no keyboard attached, click the on-screen keyboard at the bottom of
   the window. When it has keyboard focus the computer keys A to K play C5
   to C6 (MIDI 72-84, the keys labelled C4 to C5), with W, E, T, Y and U
   the sharps.

In the standalone app, click **Options** at the top of its window, choose
**Audio/MIDI Settings...**, pick an audio output and make sure your MIDI
keyboard is ticked under the MIDI inputs. The same menu saves and loads
the app's state.

A new instance starts as a spruce Dreadnought in standard tuning, played
with the fingers and heard through the stereo microphones, with a little
of the under-saddle piezo beneath them and a small room around them.

## The panel

The host can automate every knob and switch except CHORDS and PANIC. The
window can be resized from its corner and keeps its proportions.

**Top bar**

- **SUSTAIN** (off): lets every released note ring as the sustain pedal
  does, on every MIDI channel, until its string is needed or the switch is
  turned off.
- **CHORDS** (Gather Chords, off): gathers keys pressed within 30 ms of
  each other into one chord, fretted and strummed as a guitarist would;
  every note then sounds 30 ms later, which the host compensates on
  playback.
- **PANIC**: stops every string at once, clears the sustain pedal and
  returns the strum keyswitches to automatic.

**Guitar**

- **Construction**: Dreadnought, Auditorium or Parlor, each with a spruce
  top, sets Body Shape and Body Material together; the label reads
  CONSTRUCTION / CUSTOM when they match none of the three.
- **Body Shape** (Dreadnought): Parlor, Auditorium, Dreadnought or Jumbo,
  compact to large; changes the body's resonances and sustain.
- **Body Material** (Spruce): Spruce, Mahogany or Maple soundboard.
- **Tuning** (Standard): Standard, Drop D, DADGAD, Open G or Half-step
  down; each retunes the six strings themselves.

**Player**

- **Picking** (Finger): Finger is balanced, Pick crisp and nearer the
  bridge, Thumb rounder and nearer the neck.
- **String Age** (15 %): fresh strings at 0, worn and darker strings at
  100 %.
- **Pluck Position** (28 %): near the bridge at 0, toward the neck at
  100 %.
- **Touch** (58 %): softer, darker contact at 0, harder and brighter at
  100 %.
- **Release Noise** (10 %): the damping hand's contact as a note ends and
  faint noise from fingers sliding on wound strings; 0 adds none, 50 % is
  the nominal level, 100 % twice it.

**Capture & Output**

- **Capture** (Stereo mic): the stereo body microphones, one mono body
  microphone, or the under-saddle piezo through its preamp. Mono mic and
  Piezo send the same signal to both channels.
- **Body** (82 %): how strongly the microphones hear the body (0 still
  keeps it audible); the piezo ignores it.
- **Stereo** (62 %): width between the two microphones; Mono mic and Piezo
  ignore it.
- **Piezo** (20 %): the piezo mixed in beneath the microphones; no effect
  when Capture is Piezo.
- **Room** (50 %): a small studio's reflections around the microphones; 0
  is dry once the room has rung out; the piezo never hears it.
- **Output** (0 dB): final level, -24 to +6 dB.

**Strings display**: the six strings, the highest at the top. Filled
markers are held frets, outlined markers released notes, and blue-green
open markers sympathetic vibration. The legend repeats the keyswitches
and the wheels' roles.

## Playing

**Range.** Each string reaches its twentieth fret, so each tuning has its
own range: E2-C6 (Standard; MIDI 40-84), D2-C6 (Drop D; 38-84), D2-A#5
(DADGAD and Open G; 38-82), D#2-B5 (Half-step down; 39-83). Anything
outside it is ignored. The on-screen keyboard spans D2-C6 (MIDI 38-84);
its labels count middle C as C3, so its top key reads C5.

**Velocity** is how hard the string is plucked. **Release velocity** sets
how firmly the hand damps the string: 64 is nominal, softer key-ups ring
up to about twice as long, firmer ones about half; it also sets the
level of Release Noise.

**Key-up.** A released note keeps ringing for a 32nd note at the song's
tempo (62.5 ms at 120 BPM) before the hand damps it, so the same note
played again in that time continues the ringing string. Hold the key or
use sustain for longer.

**Chords and strums.** Notes that arrive together are fretted as one
chord shape and strummed, low to high on a downstroke. A strum within a
dotted eighth of the previous one (0.375 s at 120 BPM) is the hand's
return and goes the other way; after a longer gap it is a downstroke
again. Notes played by hand arrive a little apart: turn on CHORDS to
gather them, at the cost of 30 ms latency.

**Keyswitches** below the guitar's range latch the strum direction on
every channel, whichever channel sends them, until the next keyswitch or
PANIC: MIDI 24 makes every strum a downstroke, 25 an upstroke and 26
returns to automatic. They never sound. The panel's legend names them
**C0**, **C#0** and **D0**, counting middle C as C3; with middle C as C4,
as in the rest of this guide, they are C1, C#1 and D1.

**Sustain switch and sustain pedal.** CC64 holds released notes on its
channel until pedal-up; SUSTAIN does the same on every channel. They hold
independently, so a note stays held while either holds it. Neither catches
a key-up whose 32nd-note window has already started. Turning the switch
off damps the held notes as a pedal-up does; PANIC leaves the switch as it
is.

**Wheels and controllers.**

- **Mod wheel (CC1)**: vibrato from the fretting hand, fast and shallow
  at first, slower and deeper as the wheel opens, up to 20 cents. Open
  strings take none.
- **Pitch bend**: a slide, the fretting hand moving along the neck. The
  range is 2 semitones unless the controller sets another (RPN 0, up to
  96).
- **CC2**: the picking hand resting by the saddle, damping and darkening
  the strings as pressure rises. PANIC and Reset All Controllers leave it
  where it is, so a controller left at full pressure keeps the strings
  muted until it moves.
- **CC65 and CC84**: a slur instead of a new pluck. With CC65 on, send
  CC84 with the held note's number, then the new note, up to an octave
  away on the same string: the string moves to the new fret without being
  plucked again. Anything else is plucked as usual.

## MIDI implementation

All sixteen channels are independent unless an MPE zone is set, except
that CC1, CC2 and the strum keyswitches act on the whole instrument
whichever channel sends them.

| Message | What it does |
| --- | --- |
| Note On / Off | Pluck, with velocity as force; release velocity sets the damping |
| Notes 24-26 | Strum-direction keyswitches: C0, C#0, D0 as the panel names them (C1-D1 with middle C as C4) |
| Pitch bend | Slide; 2 semitones by default (RPN 0 sets 0-96) |
| CC1 | Vibrato |
| CC2 | Picking-hand damping at the saddle |
| CC64 | Sustain pedal, per channel |
| CC65 + CC84 | Slur the held note to the next Note On |
| CC74 | MPE member channels: that note's pluck position |
| Channel pressure | MPE member channels: lowers that note's vibrato depth |
| RPN 0 / RPN 6 | Pitch-bend range / MPE lower zone (RPN 6 on channel 1) |
| CC120 | All Sound Off |
| CC121 | Reset All Controllers (lifts the pedal, not CC2) |
| CC123-127 | All Notes Off; CC126 = 6 on channel 1 gives each string its own channel, CC127 ends that |
| Program change, polyphonic pressure | Ignored |

**MPE.** RPN 6 on channel 1 creates a lower zone: channel 1 then manages
the member channels above it (default bend 2 semitones on the manager, 48
on members). A member's bend pushes its string across the fret, as a
bend does, rather than sliding. The zone is not saved with a session;
MPE controllers usually send it again when they connect.

## Outputs and latency

- **Main** (stereo): whatever Capture selects, with the piezo beneath it
  at the Piezo level.
- **Piezo** (mono, optional): the piezo alone, whatever Capture selects. In
  a VST3 host enable the second output in the track's output or bus
  settings; an Audio Unit host always sees both outputs.

Acustra reports 7 samples of latency to the host, at every sample rate,
and 30 ms more while CHORDS is on (1447 samples at 48 kHz). Hosts
compensate it on playback; live playing hears it.

## CPU

Every string is computed live, so the load rises with the number of
ringing strings, the sample rate and smaller buffers. In the project's
timing run of 11 October 2026, a six-string chord re-picked again and
again at 96 kHz with 32-sample buffers did not always finish in time. If
you hear crackles, raise the buffer size or work at 44.1 or 48 kHz.

## Licences and trademarks

Acustra's own code is MIT-licensed (`LICENSE`). Acustra is built with
JUCE, includes code and data from others, and adapts measurements
published under CC BY 4.0; `THIRD_PARTY_NOTICES.md` and the `ThirdParty`
folder beside this guide give their credits and licences.

VST is a registered trademark of Steinberg Media Technologies GmbH.
