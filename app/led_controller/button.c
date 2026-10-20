#include "button.h"

void next(Button *btn, bool value) {
    uint32_t now = uptime_ms;

    btn->prev = btn->curr;

    if (value == btn->curr) {
        btn->pending = false;
        return;
    }

    if (!btn->pending) {
        btn->pending = true;
        btn->since_ms = now;
    }

    if ((uint32_t)(now - btn->since_ms) >= BUTTON_DEBOUNCE_MS) {
        btn->curr = value;
        btn->pending = false;
    }
}

bool pressed(Button *btn) {
    return !btn->prev && btn->curr;
}

bool released(Button *btn) {
    return btn->prev && !btn->curr;
}