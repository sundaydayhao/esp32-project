/*
 * minimp3_impl.c
 *
 * This file provides the single-translation-unit implementation of minimp3.
 * The minimp3.h header is a "single-header" library: defining
 * MINIMP3_IMPLEMENTATION before including it compiles the full decoder.
 *
 * minimp3 is MIT licensed:
 * https://github.com/lieff/minimp3
 */

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ALLOW_MONO_STEREO_TRANSITION
#include "minimp3.h"