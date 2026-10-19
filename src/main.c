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

static Button record_button = {0};
static Button bypass_button = {0};


int main(void)
{
    periph_init();
    fs_unit(true);
    tusb_init();

    mux_select(2, MUX_CHANNEL_X1);
    aic3104_init();
    i2s_dma_init();

    app_state_t state = IDLE;

    while (1)
    {
        ButtonStatus button_status = poll_button_events();

        if (button_status.ready == 1)
        {
            next(
                &record_button,
                button_status.VD1 == LONG_EVENT
            );

            next(
                &bypass_button,
                button_status.VD3 == LONG_EVENT
            );

            switch (state)
            {
                case IDLE:
                {
                    if (pressed(&record_button))
                    {
                        aic3104_soft_reset();
                        aic3104_init_clocking();
                        aic3104_line1lp_to_i2s();

                        FRESULT result =
                            wav_recording_start(AUDIO_FILENAME);

                        if (result == FR_OK)
                        {
                            state = RECORDING;

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
                    else if (button_status.VD2 == SHORT_EVENT)
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
                    else if (pressed(&bypass_button))
                    {
                        aic3104_soft_reset();
                        aic3104_init_clocking();
                        aic3104_init_analog_bypass();

                        state = BYPASS;

                        SEGGER_RTT_printf(
                            0,
                            "Bypass started\r\n"
                        );
                    }

                    break;
                }

                case RECORDING:
                {
                    if (released(&record_button))
                    {
                        FRESULT result = wav_recording_end();

                        state = IDLE;

                        SEGGER_RTT_printf(
                            0,
                            "WAV recording finished: %d\r\n",
                            (int)result
                        );
                    }

                    break;
                }

                case PLAYING:
                {
                    /* Кнопки не прерывают воспроизведение. */
                    break;
                }

                case BYPASS:
                {
                    if (released(&bypass_button))
                    {
                        /* Отключаем аналоговый маршрут кодека. */
                        aic3104_soft_reset();

                        state = IDLE;

                        SEGGER_RTT_printf(
                            0,
                            "Bypass finished\r\n"
                        );
                    }

                    break;
                }
            }
        }

        switch (state)
        {
            case IDLE:
            {
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
                tud_task();
                break;
            }

            case RECORDING:
            {
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_SetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
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

                break;
            }

            case PLAYING:
            {
                LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
                FRESULT result = audio_playback_task();

                if (result != FR_OK)
                {
                    /* Playback сам выполняет остановку и закрытие. */
                    state = IDLE;

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
                }

                break;
            }

            case BYPASS:
            {
                LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP1_PIN);
                LL_GPIO_ResetOutputPin(AMP_PORT, MUTE_AMP2_PIN);
                /* Аналоговый маршрут работает без обслуживания. */
                break;
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