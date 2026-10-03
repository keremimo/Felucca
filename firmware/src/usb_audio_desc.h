/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Appended to the MIDI configuration: UAC1, fixed 44.1 kHz stereo PCM16.
 * IF2 control, IF3 playback, IF4 capture. EP2 OUT playback, EP2 IN capture,
 * EP3 IN explicit feedback. No feature units or writable sample-rate control. */
    8, 0x0B, 2, 3, 1, 1, 0, 0,
    9, 4, 2, 0, 0, 1, 1, 0, 0,
    10, 0x24, 1, 0x00, 0x01, 52, 0, 2, 3, 4,
    12, 0x24, 2, 1, 0x01, 0x01, 0, 2, 3, 0, 0, 0,  /* USB streaming input */
    9, 0x24, 3, 2, 0x01, 0x03, 0, 1, 0,             /* speaker output */
    12, 0x24, 2, 3, 0x13, 0x07, 0, 2, 3, 0, 0, 0,  /* embedded synthesizer */
    9, 0x24, 3, 4, 0x01, 0x01, 0, 3, 0,             /* USB streaming output */

    9, 4, 3, 0, 0, 1, 2, 0, 0,
    9, 4, 3, 1, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,                         /* terminal 1, PCM */
    11, 0x24, 2, 1, 2, 2, 16, 1, 0x44, 0xAC, 0,
    9, 5, 0x02, 0x05, 184, 0, 1, 0, 0x83,          /* async OUT, feedback EP3 */
    7, 0x25, 1, 0, 0, 0, 0,
    9, 5, 0x83, 0x11, 3, 0, 1, 4, 0,               /* 10.14 feedback, refresh 16 ms */

    9, 4, 4, 0, 0, 1, 2, 0, 0,
    9, 4, 4, 1, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* terminal 4, PCM */
    11, 0x24, 2, 1, 2, 2, 16, 1, 0x44, 0xAC, 0,
    9, 5, 0x82, 0x05, 184, 0, 1, 0, 0,
    7, 0x25, 1, 0, 0, 0, 0,
