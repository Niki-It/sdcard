#include "led_controller.h"
#include "customcrc8/new_crc8.h"
#include "led_raw/led_raw.h"

#define SETBIT(var, bit) ((var) |= (1U << (bit)))
#define GETBIT(var, bit) (((var) >> (bit)) & 1U)
SetLedStateCommand formSetLedStateCommand();

static ButtonEvents button_events;
static Leds leds;
static SetLedStateCommand cmd;
// ---------------- Core логика ---------------------------
ButtonStatus poll_button_events()
{
    ButtonStatus button_status = {0};

    if(is_response_ready)
    {
        is_response_ready = 0;
        button_status.ready = 1;
        button_status.button_events = button_events;
    }

    return button_status;
}
void set_leds_state(Leds new_leds)
{
    leds = new_leds;
}
void send_SetLedState(USART_TypeDef *USARTx)
{
    cmd = formSetLedStateCommand();
    send_command_sync(USARTx, cmd);
}
// ------------------------ Вспомогательные функции ------------------
void LedController_PeriphInit()
{
    led_controller_usart_init();
    TIM6_Init();
}
void write_events(RawResponce response)
{
    uint16_t raw_led_data = response.data[1] | ((uint16_t)response.data[2] << 8);
    
    button_events.VD1 = (LedEvent)((raw_led_data >> 0)  & 0x03);
    button_events.VD2 = (LedEvent)((raw_led_data >> 2)  & 0x03);
    button_events.VD3 = (LedEvent)((raw_led_data >> 4)  & 0x03);
    button_events.VD4 = (LedEvent)((raw_led_data >> 6)  & 0x03);
    button_events.VD5 = (LedEvent)((raw_led_data >> 8)  & 0x03);
    button_events.VD6 = (LedEvent)((raw_led_data >> 10) & 0x03);
    button_events.VD7 = (LedEvent)((raw_led_data >> 12) & 0x03);
    button_events.VD8 = (LedEvent)((raw_led_data >> 14) & 0x03);
}

SetLedStateCommand formSetLedStateCommand()
{
    SetLedStateCommand cmd = {{0}};

    uint8_t fn = frame_number & 0x03;

    // --- data[0] ---
    if (leds.VD9) {
        SETBIT(cmd.data[0], 2);
    }
    // Запись fn (биты 0 и 1)
    if (fn & 1) SETBIT(cmd.data[0], 0);
    if (fn & 2) SETBIT(cmd.data[0], 1);

    // --- data[1] ---
    if (leds.VD8) SETBIT(cmd.data[1], 7);
    if (leds.VD7) SETBIT(cmd.data[1], 6);
    if (leds.VD6) SETBIT(cmd.data[1], 5);
    if (leds.VD5) SETBIT(cmd.data[1], 4);
    if (leds.VD4) SETBIT(cmd.data[1], 3);
    if (leds.VD3) SETBIT(cmd.data[1], 2);
    if (leds.VD2) SETBIT(cmd.data[1], 1);
    if (leds.VD1) SETBIT(cmd.data[1], 0);

    cmd.data[2] = crc(cmd.data, 2);

    return cmd;
}

