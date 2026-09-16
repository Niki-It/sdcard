#include "led_uart.h"
#include "customcrc8/new_crc8.h"

#define SETBIT(var, bit) ((var) |= (1U << (bit)))
#define GETBIT(var, bit) (((var) >> (bit)) & 1U)

SetLedStateCommand create_set_led_state(Leds leds, uint8_t frame_number)
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


LedEvents parse_events(uint16_t raw_led_data)
{
    LedEvents events;
    
    // Извлекаем каждый 2-битный блок
    // & 0x03 оставляет только младшие 2 бита после сдвига
    
    events.VD1 = (LedEvent)((raw_led_data >> 0)  & 0x03);
    events.VD2 = (LedEvent)((raw_led_data >> 2)  & 0x03);
    events.VD3 = (LedEvent)((raw_led_data >> 4)  & 0x03);
    events.VD4 = (LedEvent)((raw_led_data >> 6)  & 0x03);
    events.VD5 = (LedEvent)((raw_led_data >> 8)  & 0x03);
    events.VD6 = (LedEvent)((raw_led_data >> 10) & 0x03);
    events.VD7 = (LedEvent)((raw_led_data >> 12) & 0x03);
    events.VD8 = (LedEvent)((raw_led_data >> 14) & 0x03);
    
    return events;
}