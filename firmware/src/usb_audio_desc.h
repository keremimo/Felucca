/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Appended to the MIDI configuration: UAC1, stereo PCM16/24 at 44.1/48 kHz.
 * IF2 control, IF3 playback, IF4 capture. EP2 OUT playback, EP2 IN capture,
 * EP3 IN explicit feedback. Writable endpoint sample rates; alternate 1 = 16 bit, 2 = 24 bit. */
    8, 0x0B, 2, 3, 1, 1, 0, 0,
    9, 4, 2, 0, 0, 1, 1, 0, 0,
    10, 0x24, 1, 0x00, 0x01, 52, 0, 2, 3, 4,
    12, 0x24, 2, 1, 0x01, 0x01, 0, 2, 3, 0, 0, 0,  /* USB streaming input */
    9, 0x24, 3, 2, 0x01, 0x03, 0, 1, 0,             /* speaker output */
    12, 0x24, 2, 3, 0x13, 0x07, 0, 2, 3, 0, 0, 0,  /* embedded synthesizer */
    9, 0x24, 3, 4, 0x01, 0x01, 0, 3, 0,             /* USB streaming output */

    9, 4, 3, 0, 0, 1, 2, 0, 0,
    9, 4, 3, 1, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,                         /* PCM */
    14, 0x24, 2, 1, 2, 2, 16, 2, 0x44, 0xAC, 0, 0x80, 0xBB, 0,
    9, 5, 0x02, 0x05, 196, 0, 1, 0, 131,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 5, 0x83, 0x11, 3, 0, 1, 4, 0,               /* explicit 10.14 feedback */
    9, 4, 3, 2, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,                         /* PCM */
    14, 0x24, 2, 1, 2, 3, 24, 2, 0x44, 0xAC, 0, 0x80, 0xBB, 0,
    9, 5, 0x02, 0x05, 38, 1, 1, 0, 131,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 5, 0x83, 0x11, 3, 0, 1, 4, 0,               /* explicit 10.14 feedback */

    9, 4, 4, 0, 0, 1, 2, 0, 0,
    9, 4, 4, 1, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    14, 0x24, 2, 1, 2, 2, 16, 2, 0x44, 0xAC, 0, 0x80, 0xBB, 0,
    9, 5, 0x82, 0x05, 196, 0, 1, 0, 0,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 4, 4, 2, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    14, 0x24, 2, 1, 2, 3, 24, 2, 0x44, 0xAC, 0, 0x80, 0xBB, 0,
    9, 5, 0x82, 0x05, 38, 1, 1, 0, 0,
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
