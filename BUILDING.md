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
| `FELUCCA_UART` | 1 | TRS MIDI IN, 31250 baud (set to 0 to disable) |

TRS MIDI IN shares the USB MIDI channel routing and scale mapping. It accepts
channel messages and running status; MIDI clock, transport, system common and
SysEx are ignored on TRS. The USB serial console's `status` command reports
`trs_midi` (enabled), `trs_rx_bytes`, `trs_rx_msgs` and `trs_rx_drops` for hardware
testing. End-to-end TRS reception still needs verification with an external MIDI
source.

## Experimental USB audio mode

Build a USB Audio Class 1 + MIDI image with:

```
FELUCCA_USB_AUDIO=1 ./build.sh
```

This is a **build-time mode**, not a panel setting. It exposes stereo recording
and stereo playback at **44.1 kHz, 16-bit PCM**, usable simultaneously:

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
feedback and variable recording packet lengths to follow the I2S clock. Each
direction has a 1024-frame ring with a 512-frame target (about 11.6 ms), in
addition to the existing 256-frame I2S half-buffer and the host's buffers. This
first version prioritizes stable streaming over minimum latency. TIMER5 runs
above the audio-render interrupt in this mode to meet USB frame deadlines.

**Hardware validation is still required.** Host tests cover the descriptors,
routing, malformed packets, stream recovery and simulated clock drift; they do
not establish USB0 DMA behavior or host-driver compatibility. On an FM-1, check:

1. Enumeration as stereo input/output plus MIDI on the target OS, at 44.1 kHz.
2. Recording alone, playback alone and both together, including sustained synth
   load and simultaneous MIDI/web-editor traffic.
3. A long duplex recording for clicks, dropouts and drift; measure actual latency.
4. DAW stream stop/restart, cable reconnect and host sleep/wake. MASTER should
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
