#pragma once 

#include <stdint.h>
#include <string.h>

#include "ff.h"
#include "mp3dec.h"
#include "SEGGER_RTT.h"

#define MP3_INPUT_BUFFER_SIZE   4096
#define MP3_PCM_BUFFER_SAMPLES  2304


void MP3_TestDecode(void);