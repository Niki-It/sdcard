#pragma once
#include <stdbool.h>

typedef struct {
    bool prev;
    bool curr;
} Button;

void next(Button *btn, bool value);

bool pressed(Button *btn);
bool released(Button *btn);