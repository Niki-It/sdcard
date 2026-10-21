#pragma once

#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32f4xx_ll_utils.h"
#include <stm32f4xx_ll_bus.h>
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_tim.h"

#include "SEGGER_RTT.h"

#include "NVIC.h"
#include "gpio.h"
#include "clock.h"
#include "aic3104.h"
#include "mux.h"
#include "debounce.h"
#include "i2s/i2s2.h"
#include "tusb.h"
#include "sdio/sdio.h"
#include "led_controller/led_controller.h"
#include "led_controller/led_raw/led_raw.h"
#include "button_combo.h"
#include "storage/fs.h"
#include "../lib/Helix/src/pub/mp3dec.h"
#include "media/mp3_decoder.h"
#include "media/wav_encoder.h"
#include "media/wav_decoder.h"
#include "media/audio_playback.h"
#include "perf_counter/perf_counter.h"
#include "i2c.h"
#include "led_blinking.h"
#include "stdbool.h"


