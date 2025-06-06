/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDDEFS_H_
#define GOSOUNDDEFS_H_

/* Number of samples to match for release alignment. */
#define BLOCK_HISTORY (2)

/* Minimum remaining loop length after a crossfade */
#define REMAINING_AFTER_CROSSFADE 256

/* Max length for short loops */
#define SHORT_LOOP_LENGTH 256

/* Maximum number of blocks (1 block is nChannels samples) per frame */
#define MAX_FRAME_SIZE (2048)

/* Maximum number of channels the engine supports. This value cannot be
 * changed at present.
 */
#define MAX_OUTPUT_CHANNELS (2)

#endif /* GOSOUNDDEFS_H_ */
