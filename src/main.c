#include "soft_i2c.h"
#include "aic3104.h"
#include "main.h"
#include "mux.h"
#include "i2s/i2s2.h"

#include "tusb.h"
#include "sdio/sdio.h"
#include "led_controller/led_controller.h"
#include "led_controller/led_raw/led_raw.h"
#include "storage/fs.h"
#include "../lib/Helix/src/pub/mp3dec.h"
#include "media/mp3_decoder.h"
#include "media/wav_encoder.h"
#include "perf_counter/perf_counter.h"
#include "media/wav_decoder.h"
#include "media/audio_playback.h"
#include "led_controller/button.h"
#include "button_combo.h"


#define AMP_PORT        GPIOB
#define APB_PIN         LL_GPIO_PIN_6
#define OPR_PIN         LL_GPIO_PIN_7
#define MUTE_AMP1_PIN   LL_GPIO_PIN_8
#define MUTE_AMP2_PIN   LL_GPIO_PIN_9

uint32_t tick_counter = 1;
uint32_t message_counter = 0;
uint32_t send_counter = 0;

sd_card_info_t card_info;
void diskio_set_sd_card(sd_card_info_t *info);


void periph_init();
static volatile uint32_t uptime_ms = 0;

typedef enum
{
    IDLE,
    RECORDING,
    PLAYING,
    BYPASS
} app_state_t;

#define AUDIO_FILENAME "record.wav"

app_state_t state = IDLE;


void flush_combo(void) 
{
    for (uint8_t i = 0; i < 8; i++) {
        if (active_buttons_mask & (1U << i)) 
        {
            if(combo_events[6] == SHORT_EVENT && i == 6)
            {
                aic3104_loud_up();
                SEGGER_RTT_printf(
                    0,
                    "+\r\n"
                );
            }
            if(combo_events[7] == SHORT_EVENT  && i == 7)
            {
                aic3104_loud_down();

                SEGGER_RTT_printf(
                    0,
                    "-\r\n"
                );
            }
            if(combo_events[0] == LONG_EVENT  && i == 0 && state == IDLE)
            {
                aic3104_soft_reset();
                aic3104_init_clocking();
                aic3104_line1lp_to_i2s();

                FRESULT result =
                    wav_recording_start(AUDIO_FILENAME);
                

                if (result == FR_OK)
                {
                    state = RECORDING;
                    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);

                    SEGGER_RTT_printf(
                        0,
                        "WAV recording started\r\n"
                    );
                }
                else
                {
                    SEGGER_RTT_printf(
                        0,
                        "WAV recording start error: %d\r\n",
                        (int)result
                    );
                }


            }
            if(combo_events[0] == NO_EVENT  && i == 0 && state == RECORDING)
            {
                FRESULT result = wav_recording_end();

                state = IDLE;

                SEGGER_RTT_printf(
                    0,
                    "WAV recording finished: %d\r\n",
                    (int)result
                );
            }

            if(combo_events[1] == SHORT_EVENT  && i == 1)
            {
                aic3104_soft_reset();
                aic3104_init_clocking();
                aic3104_i2s_to_hprout();

                FRESULT result = audio_playback_start(
                    AUDIO_FILENAME,
                    AUDIO_FORMAT_WAV
                );

                if (result == FR_OK)
                {
                    state = PLAYING;
                    LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                    LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP2_PIN);

                    SEGGER_RTT_printf(
                        0,
                        "WAV playback started\r\n"
                    );
                }
                else
                {
                    SEGGER_RTT_printf(
                        0,
                        "WAV playback start error: %d\r\n",
                        (int)result
                    );
                }
            }
            if(combo_events[2] == LONG_EVENT  && i == 2 && state == IDLE)
            {
                LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
                aic3104_soft_reset();
                aic3104_init_clocking();
                aic3104_init_analog_bypass();

                state = BYPASS;

                SEGGER_RTT_printf(
                    0,
                    "Bypass started\r\n"
                );
            }
            if(combo_events[2] == NO_EVENT  && i == 2 && state == BYPASS)
            {
                state = IDLE;
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
                
                SEGGER_RTT_printf(
                    0,
                    "Bypass finished\r\n"
                );
            }
        }
    }
}
int main(void)
{
    periph_init();
    fs_unit(true);
    tusb_init();

    mux_select(2, MUX_CHANNEL_X1);
    aic3104_init();
    i2s_dma_init();

    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
    LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);


    while (1)
    {
        ButtonStatus button_status = poll_button_events();
        process_button_combinations(&button_status);

        if(state == PLAYING)
        {
            FRESULT result = audio_playback_task();

            if (result != FR_OK)
            {
                /* Playback сам выполняет остановку и закрытие. */
                state = IDLE;
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);

                SEGGER_RTT_printf(
                    0,
                    "WAV playback error: %d\r\n",
                    (int)result
                );
            }
            else if (!audio_playback_is_active())
            {
                state = IDLE;

                SEGGER_RTT_printf(
                    0,
                    "WAV playback finished\r\n"
                );
                state = IDLE;
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
            }
        }
        if(state == RECORDING)
        {
            FRESULT result = wav_recording_task();

            if (result != FR_OK)
            {
                FRESULT end_result = wav_recording_end();

                state = IDLE;

                SEGGER_RTT_printf(
                    0,
                    "WAV write error: %d, end: %d\r\n",
                    (int)result,
                    (int)end_result
                );
            }
        }

        if (can_send_command())
        {
            send_SetLedState(USART2);
        }
    }
}

void SysTick_Handler(void)
{
    uptime_ms++;

    tick_counter++;

    if (tick_counter >= 1000U)
    {
        tick_counter = 0;
    }
}
void periph_init()
{
    // Инициализация RTT (SEGGER RTT)
    SEGGER_RTT_Init();

    // Инициализация пинов и тактирования
    Clock_Init();
    // Применение настроек тактирования из регистров
    SystemCoreClockUpdate();

    SysTick_Config(SystemCoreClock / 1000);

    GPIO_Init();
    NVIC_Init();

    LedController_PeriphInit();

    soft_i2c_init();

    sd_status_t status = sd_init(&card_info);
    if (status != SD_OK)
    {
        SEGGER_RTT_printf(0, "SD init error: %d\r\n", status);
        return;
    }
    diskio_set_sd_card(&card_info);
    perf_counter_init();
}