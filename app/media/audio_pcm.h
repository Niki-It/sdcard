#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

typedef struct
{
    uint32_t bytes;
    bool end;
} audio_pcm_result_t;