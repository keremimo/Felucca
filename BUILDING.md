# Building Felucca

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

To inspect the device UI without flashing, render representative 240×240 screens
with the host renderer:

```
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/ui_preview tests/ui_preview.c -lm
mkdir -p build/ui-preview
build/host/ui_preview build/ui-preview
```

This writes PPM images for the instrument, drum kit, four-track LOOP, STEP, notes/chord and waveform
view while held and after release, mixer
level, pan and master effects views, each engine's scene, Settings and the clear confirmation. They use the
actual firmware drawing code and the STUDIO palette.

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.9-beta` makes a release build: the package identity becomes
`FM-1_909` and the version string `0.9-BETA`; the package is `build/felucca-0.9-beta.fwsc`.

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 0 | USB serial console; defaults to 1 when USB audio is disabled |
| `FELUCCA_USB_AUDIO` | 1 | experimental USB Audio Class 1 + MIDI: stereo playback, four-track recording; replaces the serial console |
| `FELUCCA_UART` | 1 | TRS MIDI IN, 31250 baud (set to 0 to disable) |

TRS MIDI IN shares the USB MIDI channel routing and scale mapping. It accepts
channel messages, running status, MIDI Clock, Start, Stop, and Continue. On
**GLO > GLOBAL > CLK**, choose **USB** or **TRS** as the clock source; **INT**
uses the panel BPM. The unselected input still plays notes and expressive
controls. System common, SysEx, and other realtime messages are ignored on TRS.
In a `FELUCCA_USB_AUDIO=0` build, the USB serial console's `status` command
reports `trs_midi` (enabled),
`trs_rx_bytes`, `trs_rx_msgs`, and `trs_rx_drops` for hardware testing.
End-to-end TRS timing still needs verification with an external MIDI source.

## Experimental USB audio mode

The default build includes USB Audio Class 1 and MIDI:

```
./build.sh
```

USB audio is selected at **build time**. With the default build,
the computer sees two audio devices next to the Felucca MIDI port, as with the
stock FM-1 firmware: **Felucca Out** (2 outputs, playback) and **Felucca In**
(4 mono inputs, recording). Each can use **16-bit or packed 24-bit PCM** at
**44.1 kHz**, with bit depth selected independently for each direction.

Either device can be switched off on the FM-1: GLO > SYSTEM knob 2 (AUDIO OUT) and
knob 3 (AUDIO IN), right for ON, left for OFF. About half a second after the knob comes
to rest, Felucca leaves the USB bus for a second and reconnects without the switched-off
device; USB MIDI is always present. The choice is saved to flash and survives power-off.

Choose the bit depth in the computer's audio-device settings (on macOS, Audio MIDI
Setup); the DAW's recording-file bit depth may be a separate setting. Use a
44.1 kHz DAW session, or host-side sample-rate conversion for other session rates.
A DAW that takes
one device for both directions needs an aggregate device (Audio MIDI Setup: +,
Create Aggregate Device) with drift correction on the device that is not the
clock source. No rebuild is needed to switch formats.
Changing bit depth clears and re-primes that direction's audio buffer, so expect
a short interruption. The rate is always 44.1 kHz;
the host selects the bit depth when it starts each stream. Settings are not
saved to Felucca's flash.

The synth remains **16-bit internally**, and I2S and USB both run at **44.1 kHz**.
Samples pass directly without firmware sample-rate conversion. The former 48 kHz
mode was removed to avoid its extra CPU work, filter tables/history and streaming
timing issues. Hosts can convert other source rates to the advertised native rate.
The 24-bit USB format does **not** add synth precision: capture pads the low eight
bits with zero, and playback discards them before mixing.

Routing is the same for every format:

- **Recording:** four isolated mono tracks are sent to the computer: input 1 =
  synth track 1, input 2 = synth track 2, input 3 = synth track 3, input 4 = drums.
  Select separate mono inputs in the DAW. Capture follows each track's level and
  inserts (DIST/SLICER on synth tracks, SLICER on drums), before pan, the shared
  chorus/delay/reverb buses, and MASTER processing. These shared effects remain
  in local monitoring; they are not mixed into the isolated recordings. MASTER
  does not change recording levels. Stems saturate at PCM16 full scale; reduce
  the track level if a loud patch clips.
- **Playback:** computer audio is mixed with the synth at the FM-1 outputs. The
  MASTER knob also controls playback level. The sum is saturated to avoid integer
  wraparound; lower the DAW output or synth levels if the sum clips.
- Computer playback is excluded from USB recording, avoiding a digital loop
  when a DAW monitors the recording input. Local synth monitoring stays active.
- USB MIDI, the web editor and MIDI-based update entry remain available. The
  three USB transmit endpoints are used by MIDI, recording and playback clock
  feedback, so the CDC serial console is unavailable. Explicitly combining
  `FELUCCA_USB_AUDIO=1` and `FELUCCA_CDC=1` is a build error.

The implementation uses asynchronous USB endpoints with explicit 10.14 playback
feedback and variable recording packet lengths to follow the I2S clock. Felucca
Out and Felucca In are separate UAC1 functions, so the host times each one from
its own endpoint. (macOS times a single duplex device from its recording
packets, so there one late recording packet also cost playback.) Each
direction has a 1024-frame ring with a 512-frame target (about 11.6 ms), in
addition to the existing 256-frame I2S half-buffer and the host's buffers. Capture
also keeps one packet prepared ahead of the active DMA packet (about 1 ms).
This version prioritizes stable streaming over minimum latency. TIMER5 runs
above the audio-render interrupt and schedules USB service by elapsed time, up
to 4 kHz. IN endpoints are rearmed before playback processing, and capture's next
packet is packed while DMA owns the current one. Counting serviced timer ticks
or waiting to prepare capture until DMA completes can miss host IN deadlines
even when every USB frame is observed.

**Validation status.** Host tests cover four-channel descriptors, both bit depths,
fixed-rate EP0 and interface requests, routing, native-rate pitch/levels, malformed
packets, stream recovery and simulated clock drift, per-channel isolation, the
real synth/drum capture taps, and prepared-packet/DMA ownership. macOS hardware
testing reproduced the old capture fault at about 853 packets/s with repeated
ring overruns. The deadline fix restored about 1000 packets/s with no new stream
errors, including a 61 s duplex run with six synth notes and drums. User listening
confirmed correct playback at 44.1 kHz; intermittent repeats remained at 48 kHz,
so that mode has been removed.
`tools/usb_audio_stats.py --window` prints the device's stream counters, USB
service time and render load over MIDI. Still to check:

1. Extended listening with real program material and heavy synth patches (the voice
   limiter sheds a voice whenever a render takes more than 85 % of its time).
2. A long duplex recording for clicks, dropouts and drift; measure actual latency.
3. Windows and Linux, cable reconnect, host sleep/wake and DAW use. MASTER should
   control local playback, and computer playback should not enter USB recording.

Flash erase/program operations mask interrupts and can interrupt streaming;
avoid saving presets/projects or uploading data during a recording. The update
loader continues to expose its existing MIDI-only interface. To return to the
standard MIDI + serial build, rebuild with `FELUCCA_USB_AUDIO=0`.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

```
tests/run_tests.sh
```

Runs the host tests (flash storage, user presets, MIDI parser, update entry, update
loader, a DSP render, the 4-track mix, project formats, the SLICER, the regression suite,
the command-line installer) and, with Node.js, the web page tests. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build).

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
