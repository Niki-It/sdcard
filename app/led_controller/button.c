#include "button.h"



void next(Button *btn, bool value) {
    btn->prev = btn->curr;
    btn->curr = value;
}

bool pressed(Button *btn) {
    return !btn->prev && btn->curr;  // 0 → 1
}

bool released(Button *btn) {
    return btn->prev && !btn->curr;  // 1 → 0
}