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

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.9-beta` makes a release build: the package identity becomes
`FM-1_909` and the version string `0.9-BETA`; the package is `build/felucca-0.9-beta.fwsc`.

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console; defaults to 0 in USB audio builds |
| `FELUCCA_USB_AUDIO` | 0 | experimental stereo USB Audio Class 1 + MIDI; replaces the serial console |
| `FELUCCA_UART` | 0 | TRS MIDI IN (not tested on hardware) |

## Experimental USB audio mode

Build a USB Audio Class 1 + MIDI image with:

```
FELUCCA_USB_AUDIO=1 ./build.sh
```

USB audio is enabled at **build time**, not from a panel setting. Once enabled,
the computer sees two audio devices next to the Felucca MIDI port, as with the
stock FM-1 firmware: **Felucca Out** (2 outputs, playback) and **Felucca In**
(2 inputs, recording). Each can use **16-bit or packed 24-bit PCM** at **44.1 or
48 kHz**, independently of the other.

Choose the formats in the computer's audio-device settings (on macOS, Audio MIDI
Setup). A DAW can also select the sample rate if its audio driver exposes that
control; its recording-file bit depth may be a separate setting. A DAW that takes
one device for both directions needs an aggregate device (Audio MIDI Setup: +,
Create Aggregate Device) with drift correction on the device that is not the
clock source. No rebuild is needed to switch formats.
Changing a format clears and re-primes that direction's audio buffer, so expect
a short interruption. The default rate after USB reset/reconnect is 44.1 kHz;
the host selects the bit depth when it starts each stream. Settings are not
saved to Felucca's flash.

The synth remains **16-bit internally**, and I2S continues at **44.1 kHz**. The 44.1 kHz USB
path passes samples directly. At 48 kHz, USB service uses a 48-tap polyphase FIR
converter with a 20 kHz cutoff, adding about 0.5 ms of filter delay per direction
and rolling off the highest frequencies. It takes about 0.1 ms of CPU time per
millisecond in each direction (0.02 ms at 44.1 kHz). This conversion preserves pitch and
timing without changing synth tables, effects or sequencing. The 24-bit USB
format does **not** add synth precision: capture pads the low eight bits with
zero, and playback discards them before conversion/mixing.

Routing is the same for every format:

- **Recording:** Felucca's stereo synth/drum mix, after effects and the MASTER
  level, is sent to the computer.
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
addition to the existing 256-frame I2S half-buffer and the host's buffers. This
first version prioritizes stable streaming over minimum latency. TIMER5 runs
above the audio-render interrupt in this mode to meet USB frame deadlines.

**Validation status.** Host tests cover the descriptors, all four formats, EP0
rate and interface requests, routing, conversion pitch/levels, rejection of a
23 kHz playback tone, malformed packets, stream recovery and simulated clock
drift. On an FM-1 with macOS, both devices enumerate with all four formats;
playback, recording and both together ran for 30–60 s in every rate combination
with no ring underruns or overruns, bad packets, missed frames or late renders,
and with no CoreAudio overloads. Format changes during streaming and repeated
stream start/stop recovered. Those runs streamed silence with the synth idle.
`tools/usb_audio_stats.py --window` prints the device's stream counters, USB
service time and render load over MIDI. Still to check:

1. Listening with real program material, and with heavy synth patches during
   48 kHz duplex (the conversion then takes about a fifth of the CPU; the voice
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
