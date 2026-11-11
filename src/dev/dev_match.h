/* The developer match (src/dev/dev_match.c). C only. */
#ifndef TD_DEV_MATCH_H
#define TD_DEV_MATCH_H

#include "rom.h"

/* Runs one CPU-vs-CPU match on the checked ROM, with no picture, and returns 0 when it reached its end (phase 4). */
int dev_match_run(const Rom *rom);

#endif
