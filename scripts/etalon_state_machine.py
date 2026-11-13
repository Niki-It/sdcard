# Импорты (НЕ МЕНЯТЬ!)
from dataclasses import dataclass, field
from enum import IntEnum

from MockSignal import get_response
from MockCombo import get_combo
from ComboContext import (
    FlushContext, source_t, ButtonEvent,
    context_has,
    B_ALERT, B_PRM, B_GGS, B_F1, B_GGS2, B_F2, B_APB, B_PLUS, B_MINUS, B_PRD, # Кнопки для context_has
    REMOTE_1, REMOTE_2, OUTSOURCE, # Источники source_t
    SHORT_EVENT, DOUBLE_SHORT, LONG_EVENT, NO_EVENT # События ButtonEvent
)


# События
class app_event_t(IntEnum):
    ACHTUNG_PRESSED = 0
    ACHTUNG_RELEASED = 1
    TRK_PRESSED = 2
    TRK_RELEASED = 3
    ALERT_TOGGLE = 4
    AUTO_RECEIVE_TOGGLE = 5
    RECEIVE_TOGGLE = 6
    RECEIVE_HOLD = 7
    RECEIVE_RELEASE = 8
    RECEIVE_FINISHED = 9
    GGS1_HOLD = 10
    GGS2_HOLD = 11
    APB1_HOLD = 12
    APB2_HOLD = 13
    GGS1_RELEASE = 14
    GGS2_RELEASE = 15
    APB1_RELEASE = 16
    APB2_RELEASE = 17
    AUTO_ANNOUNCE_1_TOGGLE = 18
    AUTO_ANNOUNCE_2_TOGGLE = 19
    ANNOUNCE_START = 20
    ANNOUNCE_FINISHED = 21
    ANNOUNCE_1_HOLD = 22
    ANNOUNCE_2_HOLD = 23
    ANNOUNCE_1_RELEASE = 24
    ANNOUNCE_2_RELEASE = 25
    PHRASE_1_PRM = 26
    PHRASE_1_GGS1 = 27
    PHRASE_1_GGS2 = 28
    PHRASE_1_APB1 = 29
    PHRASE_1_APB2 = 30
    PHRASE_2_PRM = 31
    PHRASE_2_GGS1 = 32
    PHRASE_2_GGS2 = 33
    PHRASE_2_APB1 = 34
    PHRASE_2_APB2 = 35
    PHRASE_FINISHED = 36
    RECORD_UNLOCK = 37
    RECORD_UNLOCK_EXPIRED = 38
    RECORD_CHECK_COMBINATION = 39
    VOLUME_UP = 40
    VOLUME_DOWN = 41

ACHTUNG_PRESSED = app_event_t.ACHTUNG_PRESSED
ACHTUNG_RELEASED = app_event_t.ACHTUNG_RELEASED
TRK_PRESSED = app_event_t.TRK_PRESSED
TRK_RELEASED = app_event_t.TRK_RELEASED
ALERT_TOGGLE = app_event_t.ALERT_TOGGLE
AUTO_RECEIVE_TOGGLE = app_event_t.AUTO_RECEIVE_TOGGLE
RECEIVE_TOGGLE = app_event_t.RECEIVE_TOGGLE
RECEIVE_HOLD = app_event_t.RECEIVE_HOLD
RECEIVE_RELEASE = app_event_t.RECEIVE_RELEASE
RECEIVE_FINISHED = app_event_t.RECEIVE_FINISHED
GGS1_HOLD = app_event_t.GGS1_HOLD
GGS2_HOLD = app_event_t.GGS2_HOLD
APB1_HOLD = app_event_t.APB1_HOLD
APB2_HOLD = app_event_t.APB2_HOLD
GGS1_RELEASE = app_event_t.GGS1_RELEASE
GGS2_RELEASE = app_event_t.GGS2_RELEASE
APB1_RELEASE = app_event_t.APB1_RELEASE
APB2_RELEASE = app_event_t.APB2_RELEASE
AUTO_ANNOUNCE_1_TOGGLE = app_event_t.AUTO_ANNOUNCE_1_TOGGLE
AUTO_ANNOUNCE_2_TOGGLE = app_event_t.AUTO_ANNOUNCE_2_TOGGLE
ANNOUNCE_START = app_event_t.ANNOUNCE_START
ANNOUNCE_FINISHED = app_event_t.ANNOUNCE_FINISHED
ANNOUNCE_1_HOLD = app_event_t.ANNOUNCE_1_HOLD
ANNOUNCE_2_HOLD = app_event_t.ANNOUNCE_2_HOLD
ANNOUNCE_1_RELEASE = app_event_t.ANNOUNCE_1_RELEASE
ANNOUNCE_2_RELEASE = app_event_t.ANNOUNCE_2_RELEASE
PHRASE_1_PRM = app_event_t.PHRASE_1_PRM
PHRASE_1_GGS1 = app_event_t.PHRASE_1_GGS1
PHRASE_1_GGS2 = app_event_t.PHRASE_1_GGS2
PHRASE_1_APB1 = app_event_t.PHRASE_1_APB1
PHRASE_1_APB2 = app_event_t.PHRASE_1_APB2
PHRASE_2_PRM = app_event_t.PHRASE_2_PRM
PHRASE_2_GGS1 = app_event_t.PHRASE_2_GGS1
PHRASE_2_GGS2 = app_event_t.PHRASE_2_GGS2
PHRASE_2_APB1 = app_event_t.PHRASE_2_APB1
PHRASE_2_APB2 = app_event_t.PHRASE_2_APB2
PHRASE_FINISHED = app_event_t.PHRASE_FINISHED
RECORD_UNLOCK = app_event_t.RECORD_UNLOCK
RECORD_UNLOCK_EXPIRED = app_event_t.RECORD_UNLOCK_EXPIRED
RECORD_CHECK_COMBINATION = app_event_t.RECORD_CHECK_COMBINATION
VOLUME_UP = app_event_t.VOLUME_UP
VOLUME_DOWN = app_event_t.VOLUME_DOWN


# Режимы (IDLE — отсутствие активных битов)
class app_mode_t(IntEnum):
    ALERT = 0
    ACHTUNG = 1
    RECEIVE = 2
    GGS1 = 3
    GGS2 = 4
    APB1 = 5
    APB2 = 6
    ANNOUNCE = 7
    PHRASE = 8
    RECORD = 9

ALERT = app_mode_t.ALERT
ACHTUNG = app_mode_t.ACHTUNG
RECEIVE = app_mode_t.RECEIVE
GGS1 = app_mode_t.GGS1
GGS2 = app_mode_t.GGS2
APB1 = app_mode_t.APB1
APB2 = app_mode_t.APB2
ANNOUNCE = app_mode_t.ANNOUNCE
PHRASE = app_mode_t.PHRASE
RECORD = app_mode_t.RECORD


# Независимые значения громкости; индексы массива state.volume.
class volume_target_t(IntEnum):
    RECEIVE = 0
    GGS1 = 1
    GGS2 = 2
    APB1 = 3
    APB2 = 4
    ANNOUNCE = 5
    PHRASE_1 = 6
    PHRASE_2 = 7


# Макросы
## Основные (переносятся на stm32)
INITIAL_ACTIVE_MODES = 0
MODE_COUNT = 10
VOLUME_COUNT = 8
ACHTUNG_INITIAL_LEVEL = True
TRK_INITIAL_LEVEL = True
CHANNEL_1 = 1 << 0
CHANNEL_2 = 1 << 1
CHANNEL_PRM = 1 << 2
ANNOUNCE_CHANNELS = CHANNEL_1 | CHANNEL_2  # CHANNEL_1, CHANNEL_2 или оба.
# True: нужны все выбранные каналы; вытеснение одного завершает всё оповещение.
# False: запуск на свободных каналах; вытеснение освобождает только занятый канал.
ANNOUNCE_REQUIRE_ALL_CHANNELS = True

## Вспомогательные (используются только для эмулятора)
TESTING_CYCLES = 1115
RECEIVE_TIMEOUT_CYCLES = 3
AUTO_ANNOUNCE_1_PERIOD = 5
AUTO_ANNOUNCE_2_PERIOD = 7
AUTO_ANNOUNCE_3_PERIOD = 9
ANNOUNCE_DURATION_CYCLES = 3
PHRASE_DURATION_CYCLES = 3
UNLOCK_RECORD_TIMEOUT_CYCLES = 5
VOLUME_INITIAL = 50
VOLUME_MIN = 0
VOLUME_MAX = 100
VOLUME_STEP = 5


# Таблица совместимости режимов
MODE_COMPATIBILITY = (
    0,  # ALERT: несовместим со всеми остальными режимами.
    0,  # ACHTUNG: несовместим со всеми остальными режимами.
    (1 << ANNOUNCE) | (1 << PHRASE),  # RECEIVE
    (1 << GGS2) | (1 << APB2) | (1 << ANNOUNCE) | (1 << PHRASE),  # GGS1
    (1 << GGS1) | (1 << APB1) | (1 << ANNOUNCE) | (1 << PHRASE),  # GGS2
    (1 << GGS2) | (1 << APB2) | (1 << ANNOUNCE) | (1 << PHRASE),  # APB1
    (1 << GGS1) | (1 << APB1) | (1 << ANNOUNCE) | (1 << PHRASE),  # APB2
    (1 << RECEIVE) | (1 << GGS1) | (1 << GGS2) | (1 << APB1) | (1 << APB2) | (1 << PHRASE),
    (1 << RECEIVE) | (1 << GGS1) | (1 << GGS2) | (1 << APB1) | (1 << APB2) | (1 << ANNOUNCE),
    0,  # RECORD: несовместим со всеми активными режимами.
)
# Совместимость ANNOUNCE и PHRASE с остальными режимами зависит от занятых каналов.
# Эту проверку и вытеснение выполняет process_state_event().


# Параметры режимов
@dataclass
class ModeParams:
    source: source_t = OUTSOURCE    # Источник активации
    is_holding: bool = False        # Требует удержания кнопки на пульте


# Состояние устройства
@dataclass
class AppState:
    active_modes: int = INITIAL_ACTIVE_MODES
    modes: list = field(default_factory=lambda: [ModeParams() for _ in range(MODE_COUNT)])
    last_achtung: bool = ACHTUNG_INITIAL_LEVEL
    last_trk: bool = TRK_INITIAL_LEVEL
    auto_receive: bool = False      # Автоматика не входит в active_modes.
    receive_timeout_count: int = 0
    auto_announce_1: bool = False
    auto_announce_2: bool = False
    auto_announce_1_count: int = 0
    auto_announce_2_count: int = 0
    auto_announce_3_count: int = 0
    announce_channels: int = 0
    announce_count: int = 0
    announce_button: int = -1       # Кнопка, начавшая ручное удержание.
    phrase_number: int = 0
    phrase_output: int = -1         # RECEIVE (ПРМ), GGS1/2 или APB1/2.
    phrase_channel: int = 0
    phrase_count: int = 0
    unlock_record: bool = False
    unlock_record_count: int = 0
    record_button: int = -1
    record_invalid: bool = False
    volume: list = field(default_factory=lambda: [VOLUME_INITIAL] * VOLUME_COUNT)

state = AppState()


# Ответы обработчиков (НЕ МЕНЯТЬ!)
class handler_responce_t(IntEnum):
    PROCESSING = 0
    FINISHED = 1
    ERROR = 2

PROCESSING = handler_responce_t.PROCESSING
FINISHED = handler_responce_t.FINISHED
ERROR = handler_responce_t.ERROR


# Моковые данные (НЕ МЕНЯТЬ!)
## Внешний сигнал achtung
def process_achtung_button():
    current_signal = bool(get_response('achtung'))
    if current_signal == state.last_achtung:
        return
    state.last_achtung = current_signal
    if current_signal == 0:
        process_state_event(ACHTUNG_PRESSED, OUTSOURCE)
    else:
        process_state_event(ACHTUNG_RELEASED, OUTSOURCE)

## Внешний сигнал trk
def process_trk_button():
    current_signal = bool(get_response('trk'))
    if current_signal == state.last_trk:
        return
    state.last_trk = current_signal
    if current_signal == 0:
        process_state_event(TRK_PRESSED, OUTSOURCE)
    else:
        process_state_event(TRK_RELEASED, OUTSOURCE)

## Сигналы пультов 1 и 2
def process_button_combinations():
    remote = [
        get_combo("context_1"),
        get_combo("context_2")
    ]
    for context in remote:
        if context != False and isinstance(context, FlushContext):
            flush_combo(context)


# Основная логика
## Вспомогательные функции, обработчики
def mode_is_active(mode):
    return (state.active_modes & (1 << mode)) != 0

def modes_are_compatible(first, second):
    # Требуем взаимную совместимость; ошибка одной строки не разрешит конфликт.
    return ((MODE_COMPATIBILITY[first] & (1 << second)) != 0
            and (MODE_COMPATIBILITY[second] & (1 << first)) != 0)

# Проверки допустимости выполняет process_state_event().
# Здесь при переносе на C добавятся действия с модулями устройства.
def start_alert(source):
    state.modes[ALERT].source = source
    state.modes[ALERT].is_holding = False
    state.active_modes |= 1 << ALERT
    print("  ALERT: включён, источник", source.name)


def end_alert():
    state.active_modes &= ~(1 << ALERT)
    state.modes[ALERT].source = OUTSOURCE
    state.modes[ALERT].is_holding = False
    print("  ALERT: выключен")


def start_achtung():
    state.modes[ACHTUNG].source = OUTSOURCE
    state.modes[ACHTUNG].is_holding = False
    state.active_modes |= 1 << ACHTUNG
    print("  ACHTUNG: включён внешним сигналом")


def end_achtung():
    state.active_modes &= ~(1 << ACHTUNG)
    state.modes[ACHTUNG].source = OUTSOURCE
    state.modes[ACHTUNG].is_holding = False
    print("  ACHTUNG: выключен")


def start_auto_receive():
    state.auto_receive = True
    print("  AUTO_RECEIVE: включена")


def end_auto_receive():
    state.auto_receive = False
    print("  AUTO_RECEIVE: выключена")


def start_receive(source, is_holding):
    state.modes[RECEIVE].source = source
    state.modes[RECEIVE].is_holding = is_holding
    state.receive_timeout_count = 0
    state.active_modes |= 1 << RECEIVE
    print("  RECEIVE: включён, источник", source.name, "удержание", is_holding)


def end_receive():
    state.active_modes &= ~(1 << RECEIVE)
    state.modes[RECEIVE].source = OUTSOURCE
    state.modes[RECEIVE].is_holding = False
    state.receive_timeout_count = 0
    print("  RECEIVE: выключен")


def start_transmission(mode, source):
    # Здесь добавится включение нужного выхода соответствующего канала.
    state.modes[mode].source = source
    state.modes[mode].is_holding = True
    state.active_modes |= 1 << mode
    print("  " + app_mode_t(mode).name + ": включён, источник", source.name)


def end_transmission(mode):
    # Здесь добавится выключение соответствующего канала.
    state.active_modes &= ~(1 << mode)
    state.modes[mode].source = OUTSOURCE
    state.modes[mode].is_holding = False
    print("  " + app_mode_t(mode).name + ": выключен")


def set_auto_announce_1(enabled):
    state.auto_announce_1 = enabled
    print("  AUTO_ANNOUNCE_1:", enabled)


def set_auto_announce_2(enabled):
    state.auto_announce_2 = enabled
    print("  AUTO_ANNOUNCE_2:", enabled)


def reset_auto_announce_counters():
    state.auto_announce_1_count = 0
    state.auto_announce_2_count = 0
    state.auto_announce_3_count = 0


def start_announce(channels, source, button):
    state.modes[ANNOUNCE].source = source
    state.modes[ANNOUNCE].is_holding = source != OUTSOURCE
    state.announce_button = button
    state.announce_channels = channels
    state.announce_count = 0
    state.active_modes |= 1 << ANNOUNCE
    print("  ANNOUNCE: включён, маска каналов", channels, "источник", source.name,
          "удержание", state.modes[ANNOUNCE].is_holding)


def end_announce():
    state.active_modes &= ~(1 << ANNOUNCE)
    state.modes[ANNOUNCE].source = OUTSOURCE
    state.modes[ANNOUNCE].is_holding = False
    state.announce_channels = 0
    state.announce_count = 0
    state.announce_button = -1
    print("  ANNOUNCE: выключен")


def set_announce_channels(channels):
    # Оставшиеся каналы продолжают оповещение без перезапуска его обработчика.
    state.announce_channels = channels
    print("  ANNOUNCE: оставлена маска каналов", channels)


def process_auto_announce_1():
    state.auto_announce_1_count += 1
    if state.auto_announce_1_count >= AUTO_ANNOUNCE_1_PERIOD:
        state.auto_announce_1_count = 0
        return PROCESSING
    return FINISHED


def process_auto_announce_2():
    state.auto_announce_2_count += 1
    if state.auto_announce_2_count >= AUTO_ANNOUNCE_2_PERIOD:
        state.auto_announce_2_count = 0
        return PROCESSING
    return FINISHED


def process_auto_announce_3():
    state.auto_announce_3_count += 1
    if state.auto_announce_3_count >= AUTO_ANNOUNCE_3_PERIOD:
        state.auto_announce_3_count = 0
        return PROCESSING
    return FINISHED


def process_manual_announce():
    return PROCESSING


def process_announce():
    state.announce_count += 1
    if state.announce_count >= ANNOUNCE_DURATION_CYCLES:
        return FINISHED
    return PROCESSING


def start_phrase(source, number, output, channel):
    state.modes[PHRASE].source = source
    state.modes[PHRASE].is_holding = False
    state.phrase_number = number
    state.phrase_output = output
    state.phrase_channel = channel
    state.phrase_count = 0
    state.active_modes |= 1 << PHRASE
    print("  PHRASE: включена, номер", number, "выход", app_mode_t(output).name,
          "источник", source.name)


def end_phrase():
    state.active_modes &= ~(1 << PHRASE)
    state.modes[PHRASE].source = OUTSOURCE
    state.modes[PHRASE].is_holding = False
    state.phrase_number = 0
    state.phrase_output = -1
    state.phrase_channel = 0
    state.phrase_count = 0
    print("  PHRASE: выключена")


def process_phrase():
    state.phrase_count += 1
    if state.phrase_count >= PHRASE_DURATION_CYCLES:
        return FINISHED
    return PROCESSING


def enable_record_unlock():
    state.unlock_record = True
    state.unlock_record_count = 0
    print("  UNLOCK_RECORD: разрешена запись")


def clear_record_unlock():
    state.unlock_record = False
    state.unlock_record_count = 0
    print("  UNLOCK_RECORD: таймаут истёк")


def start_record(source, button):
    state.modes[RECORD].source = source
    state.modes[RECORD].is_holding = True
    state.record_button = button
    state.record_invalid = False
    state.unlock_record = False
    state.unlock_record_count = 0
    state.active_modes |= 1 << RECORD
    print("  RECORD: включена, кнопка", button, "источник", source.name)


def invalidate_record():
    state.record_invalid = True
    print("  RECORD: посторонняя комбинация владельца, запись помечена некорректной")


def end_record(invalid):
    # Здесь будет завершение записи и её сохранение либо отбрасывание по invalid.
    print("  RECORD: завершена, кнопка", state.record_button,
          "источник", state.modes[RECORD].source.name, "invalid", invalid)
    state.active_modes &= ~(1 << RECORD)
    state.modes[RECORD].source = OUTSOURCE
    state.modes[RECORD].is_holding = False
    state.record_button = -1
    state.record_invalid = False


def process_record():
    # Периодическая обработка записи. Завершение — только по событию.
    return PROCESSING


def process_record_unlock():
    state.unlock_record_count += 1
    if state.unlock_record_count >= UNLOCK_RECORD_TIMEOUT_CYCLES:
        return FINISHED
    return PROCESSING


def set_volume(target, value):
    state.volume[target] = value
    print("  VOLUME:", volume_target_t(target).name, "=", value)


def process_receive():
    return PROCESSING


def process_auto_receive():
    if state.last_trk == 0:
        state.receive_timeout_count = 0
        return PROCESSING
    state.receive_timeout_count += 1
    if state.receive_timeout_count >= RECEIVE_TIMEOUT_CYCLES:
        return FINISHED
    return PROCESSING


def process_alert():
    # Периодическое обслуживание пока не требуется.
    return PROCESSING


def process_achtung():
    # Периодическое обслуживание пока не требуется.
    return PROCESSING


## Преобразование комбинации в событие основной логики
def flush_combo(context: FlushContext):
    # Проверка записи выполняется до действий комбинации, включая отпускание.
    # Одиночное продолжение удержания/отпускание той же кнопки не портит запись.
    record_button = -1
    if (context.count == 1
            and (context_has(context, B_F1, LONG_EVENT)
                 or context_has(context, B_F1, NO_EVENT))):
        record_button = B_F1
    if (context.count == 1
            and (context_has(context, B_F2, LONG_EVENT)
                 or context_has(context, B_F2, NO_EVENT))):
        record_button = B_F2
    if context.count > 0:
        process_state_event(RECORD_CHECK_COMBINATION, context.remote, record_button)

    if (context.count == 2
            and context_has(context, B_F1, LONG_EVENT)
            and context_has(context, B_F2, LONG_EVENT)):
        process_state_event(RECORD_UNLOCK, context.remote)

    # Каждое правило независимо и полностью задаёт своё условие.
    # Для нажатий count строго равен числу кнопок правила.
    # Для отпускания NO_EVENT count не проверяется.
    if (context.count == 1
            and context_has(context, B_ALERT, SHORT_EVENT)):
        process_state_event(ALERT_TOGGLE, context.remote)
    if context.count == 1 and context_has(context, B_PRM, DOUBLE_SHORT):
        process_state_event(AUTO_RECEIVE_TOGGLE, context.remote)
    if context.count == 1 and context_has(context, B_PRM, SHORT_EVENT):
        process_state_event(RECEIVE_TOGGLE, context.remote)
    if context.count == 1 and context_has(context, B_PRM, LONG_EVENT):
        process_state_event(RECEIVE_HOLD, context.remote)
    if context_has(context, B_PRM, NO_EVENT):
        process_state_event(RECEIVE_RELEASE, context.remote)

    if context.count == 1 and context_has(context, B_F1, DOUBLE_SHORT):
        process_state_event(AUTO_ANNOUNCE_1_TOGGLE, context.remote)
    if context.count == 1 and context_has(context, B_F2, DOUBLE_SHORT):
        process_state_event(AUTO_ANNOUNCE_2_TOGGLE, context.remote)

    if context.count == 1 and context_has(context, B_F1, LONG_EVENT):
        process_state_event(ANNOUNCE_1_HOLD, context.remote)
    if context.count == 1 and context_has(context, B_F2, LONG_EVENT):
        process_state_event(ANNOUNCE_2_HOLD, context.remote)
    if context_has(context, B_F1, NO_EVENT):
        process_state_event(ANNOUNCE_1_RELEASE, context.remote)
    if context_has(context, B_F2, NO_EVENT):
        process_state_event(ANNOUNCE_2_RELEASE, context.remote)

    if context.count == 1 and context_has(context, B_GGS, LONG_EVENT):
        process_state_event(GGS1_HOLD, context.remote)
    if context.count == 1 and context_has(context, B_GGS2, LONG_EVENT):
        process_state_event(GGS2_HOLD, context.remote)
    if context.count == 1 and context_has(context, B_APB, LONG_EVENT):
        process_state_event(APB1_HOLD, context.remote)
    if context.count == 1 and context_has(context, B_PRD, LONG_EVENT):
        process_state_event(APB2_HOLD, context.remote)

    # Каждая пара посылает два независимых запроса: допустима частичная активация.
    if (context.count == 2
            and context_has(context, B_GGS, LONG_EVENT)
            and context_has(context, B_GGS2, LONG_EVENT)):
        process_state_event(GGS1_HOLD, context.remote)
        process_state_event(GGS2_HOLD, context.remote)
    if (context.count == 2
            and context_has(context, B_GGS, LONG_EVENT)
            and context_has(context, B_PRD, LONG_EVENT)):
        process_state_event(GGS1_HOLD, context.remote)
        process_state_event(APB2_HOLD, context.remote)
    if (context.count == 2
            and context_has(context, B_APB, LONG_EVENT)
            and context_has(context, B_GGS2, LONG_EVENT)):
        process_state_event(APB1_HOLD, context.remote)
        process_state_event(GGS2_HOLD, context.remote)
    if (context.count == 2
            and context_has(context, B_APB, LONG_EVENT)
            and context_has(context, B_PRD, LONG_EVENT)):
        process_state_event(APB1_HOLD, context.remote)
        process_state_event(APB2_HOLD, context.remote)

    if context_has(context, B_GGS, NO_EVENT):
        process_state_event(GGS1_RELEASE, context.remote)
    if context_has(context, B_GGS2, NO_EVENT):
        process_state_event(GGS2_RELEASE, context.remote)
    if context_has(context, B_APB, NO_EVENT):
        process_state_event(APB1_RELEASE, context.remote)
    if context_has(context, B_PRD, NO_EVENT):
        process_state_event(APB2_RELEASE, context.remote)

    # Одна фраза и один выход: обе кнопки дают SHORT_EVENT.
    if (context.count == 2
            and context_has(context, B_F1, SHORT_EVENT)
            and context_has(context, B_PRM, SHORT_EVENT)):
        process_state_event(PHRASE_1_PRM, context.remote)
    if (context.count == 2
            and context_has(context, B_F1, SHORT_EVENT)
            and context_has(context, B_GGS, SHORT_EVENT)):
        process_state_event(PHRASE_1_GGS1, context.remote)
    if (context.count == 2
            and context_has(context, B_F1, SHORT_EVENT)
            and context_has(context, B_GGS2, SHORT_EVENT)):
        process_state_event(PHRASE_1_GGS2, context.remote)
    if (context.count == 2
            and context_has(context, B_F1, SHORT_EVENT)
            and context_has(context, B_APB, SHORT_EVENT)):
        process_state_event(PHRASE_1_APB1, context.remote)
    if (context.count == 2
            and context_has(context, B_F1, SHORT_EVENT)
            and context_has(context, B_PRD, SHORT_EVENT)):
        process_state_event(PHRASE_1_APB2, context.remote)
    if (context.count == 2
            and context_has(context, B_F2, SHORT_EVENT)
            and context_has(context, B_PRM, SHORT_EVENT)):
        process_state_event(PHRASE_2_PRM, context.remote)
    if (context.count == 2
            and context_has(context, B_F2, SHORT_EVENT)
            and context_has(context, B_GGS, SHORT_EVENT)):
        process_state_event(PHRASE_2_GGS1, context.remote)
    if (context.count == 2
            and context_has(context, B_F2, SHORT_EVENT)
            and context_has(context, B_GGS2, SHORT_EVENT)):
        process_state_event(PHRASE_2_GGS2, context.remote)
    if (context.count == 2
            and context_has(context, B_F2, SHORT_EVENT)
            and context_has(context, B_APB, SHORT_EVENT)):
        process_state_event(PHRASE_2_APB1, context.remote)
    if (context.count == 2
            and context_has(context, B_F2, SHORT_EVENT)
            and context_has(context, B_PRD, SHORT_EVENT)):
        process_state_event(PHRASE_2_APB2, context.remote)

    # Регулировка: только одна удерживаемая кнопка и один короткий PLUS/MINUS.
    if (context.count == 2
            and context_has(context, B_GGS, LONG_EVENT)
            and context_has(context, B_PLUS, SHORT_EVENT)):
        process_state_event(VOLUME_UP, context.remote, B_GGS)
    if (context.count == 2
            and context_has(context, B_GGS, LONG_EVENT)
            and context_has(context, B_MINUS, SHORT_EVENT)):
        process_state_event(VOLUME_DOWN, context.remote, B_GGS)
    if (context.count == 2
            and context_has(context, B_GGS2, LONG_EVENT)
            and context_has(context, B_PLUS, SHORT_EVENT)):
        process_state_event(VOLUME_UP, context.remote, B_GGS2)
    if (context.count == 2
            and context_has(context, B_GGS2, LONG_EVENT)
            and context_has(context, B_MINUS, SHORT_EVENT)):
        process_state_event(VOLUME_DOWN, context.remote, B_GGS2)
    if (context.count == 2
            and context_has(context, B_APB, LONG_EVENT)
            and context_has(context, B_PLUS, SHORT_EVENT)):
        process_state_event(VOLUME_UP, context.remote, B_APB)
    if (context.count == 2
            and context_has(context, B_APB, LONG_EVENT)
            and context_has(context, B_MINUS, SHORT_EVENT)):
        process_state_event(VOLUME_DOWN, context.remote, B_APB)
    if (context.count == 2
            and context_has(context, B_PRD, LONG_EVENT)
            and context_has(context, B_PLUS, SHORT_EVENT)):
        process_state_event(VOLUME_UP, context.remote, B_PRD)
    if (context.count == 2
            and context_has(context, B_PRD, LONG_EVENT)
            and context_has(context, B_MINUS, SHORT_EVENT)):
        process_state_event(VOLUME_DOWN, context.remote, B_PRD)
    if (context.count == 2
            and context_has(context, B_PRM, LONG_EVENT)
            and context_has(context, B_PLUS, SHORT_EVENT)):
        process_state_event(VOLUME_UP, context.remote, B_PRM)
    if (context.count == 2
            and context_has(context, B_PRM, LONG_EVENT)
            and context_has(context, B_MINUS, SHORT_EVENT)):
        process_state_event(VOLUME_DOWN, context.remote, B_PRM)
    if (context.count == 2
            and context_has(context, B_F1, LONG_EVENT)
            and context_has(context, B_PLUS, SHORT_EVENT)):
        process_state_event(VOLUME_UP, context.remote, B_F1)
    if (context.count == 2
            and context_has(context, B_F1, LONG_EVENT)
            and context_has(context, B_MINUS, SHORT_EVENT)):
        process_state_event(VOLUME_DOWN, context.remote, B_F1)
    if (context.count == 2
            and context_has(context, B_F2, LONG_EVENT)
            and context_has(context, B_PLUS, SHORT_EVENT)):
        process_state_event(VOLUME_UP, context.remote, B_F2)
    if (context.count == 2
            and context_has(context, B_F2, LONG_EVENT)
            and context_has(context, B_MINUS, SHORT_EVENT)):
        process_state_event(VOLUME_DOWN, context.remote, B_F2)


## Обработка события
def process_state_event(event: app_event_t, source: source_t, button=-1):
    # Все условия входа/выхода проверяются здесь.
    # Очередей запросов и восстановления вытесненных режимов нет.
    if event == RECORD_CHECK_COMBINATION:
        if (mode_is_active(RECORD) and state.modes[RECORD].source == source
                and button != state.record_button and not state.record_invalid):
            invalidate_record()

    elif event == RECORD_UNLOCK:
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if (mode_is_active(RECORD)
                or (mode_is_active(ANNOUNCE) and state.modes[ANNOUNCE].is_holding)):
            print("  UNLOCK_RECORD: запрос отклонён — активно удержание F1/F2")
            return
        enable_record_unlock()

    elif event == RECORD_UNLOCK_EXPIRED:
        if source == OUTSOURCE and state.unlock_record:
            clear_record_unlock()

    elif event == ALERT_TOGGLE:
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if mode_is_active(ACHTUNG):
            print("  ALERT: запрос", source.name, "отклонён — активен ACHTUNG")
            return
        if mode_is_active(ALERT):
            print("  ALERT: выключение по запросу", source.name)
            end_alert()
        else:
            if mode_is_active(RECEIVE):
                print("  RECEIVE: вытеснен режимом ALERT")
                end_receive()
            for mode in (GGS1, GGS2, APB1, APB2):
                if mode_is_active(mode):
                    print("  " + app_mode_t(mode).name + ": вытеснен режимом ALERT")
                    end_transmission(mode)
            if mode_is_active(ANNOUNCE):
                print("  ANNOUNCE: вытеснен режимом ALERT")
                end_announce()
            if mode_is_active(PHRASE):
                print("  PHRASE: вытеснена режимом ALERT")
                end_phrase()
            if mode_is_active(RECORD):
                print("  RECORD: вытеснена режимом ALERT")
                end_record(True)
            start_alert(source)

    elif event == ACHTUNG_PRESSED:
        if source != OUTSOURCE or mode_is_active(ACHTUNG):
            return
        # ACHTUNG имеет высший приоритет. ALERT не восстанавливается.
        if mode_is_active(ALERT):
            print("  ALERT: вытеснен режимом ACHTUNG")
            end_alert()
        if mode_is_active(RECEIVE):
            print("  RECEIVE: вытеснен режимом ACHTUNG")
            end_receive()
        for mode in (GGS1, GGS2, APB1, APB2):
            if mode_is_active(mode):
                print("  " + app_mode_t(mode).name + ": вытеснен режимом ACHTUNG")
                end_transmission(mode)
        if mode_is_active(ANNOUNCE):
            print("  ANNOUNCE: вытеснен режимом ACHTUNG")
            end_announce()
        if mode_is_active(PHRASE):
            print("  PHRASE: вытеснена режимом ACHTUNG")
            end_phrase()
        if mode_is_active(RECORD):
            print("  RECORD: вытеснена режимом ACHTUNG")
            end_record(True)
        start_achtung()

    elif event == ACHTUNG_RELEASED:
        if source != OUTSOURCE or not mode_is_active(ACHTUNG):
            return
        end_achtung()

    elif event == AUTO_RECEIVE_TOGGLE:
        if state.auto_receive:
            if mode_is_active(RECEIVE) and state.modes[RECEIVE].source == OUTSOURCE:
                end_receive()
            end_auto_receive()
        else:
            start_auto_receive()

    elif event == TRK_PRESSED:
        if source != OUTSOURCE or not state.auto_receive:
            return
        if mode_is_active(RECEIVE):
            return
        for mode in range(MODE_COUNT):
            if (mode_is_active(mode) and mode != RECORD
                    and not modes_are_compatible(RECEIVE, mode)):
                print("  RECEIVE: запрос OUTSOURCE отклонён — активен", app_mode_t(mode).name)
                return
        if mode_is_active(PHRASE) and state.phrase_channel == CHANNEL_PRM:
            print("  PHRASE: вытеснена режимом RECEIVE")
            end_phrase()
        if mode_is_active(RECORD):
            print("  RECORD: вытеснена режимом RECEIVE")
            end_record(True)
        start_receive(OUTSOURCE, False)

    elif event == RECEIVE_TOGGLE or event == RECEIVE_HOLD:
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if mode_is_active(RECEIVE):
            # LONG_EVENT не меняет источник или способ управления активным режимом.
            if event == RECEIVE_TOGGLE and not state.modes[RECEIVE].is_holding:
                end_receive()
            return
        for mode in range(MODE_COUNT):
            if (mode_is_active(mode) and mode != RECORD
                    and not modes_are_compatible(RECEIVE, mode)):
                print("  RECEIVE: запрос", source.name, "отклонён — активен", app_mode_t(mode).name)
                return
        if mode_is_active(PHRASE) and state.phrase_channel == CHANNEL_PRM:
            print("  PHRASE: вытеснена режимом RECEIVE")
            end_phrase()
        if mode_is_active(RECORD):
            print("  RECORD: вытеснена режимом RECEIVE")
            end_record(True)
        start_receive(source, event == RECEIVE_HOLD)

    elif event == RECEIVE_RELEASE:
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if not mode_is_active(RECEIVE):
            return
        if (state.modes[RECEIVE].source == OUTSOURCE
                or (state.modes[RECEIVE].is_holding
                    and state.modes[RECEIVE].source == source)):
            end_receive()

    elif event == RECEIVE_FINISHED:
        if (source == OUTSOURCE and mode_is_active(RECEIVE)
                and state.modes[RECEIVE].source == OUTSOURCE):
            print("  RECEIVE: истёк таймаут после пропадания trk")
            end_receive()

    elif event in (GGS1_HOLD, GGS2_HOLD, APB1_HOLD, APB2_HOLD):
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if event == GGS1_HOLD:
            mode = GGS1
        elif event == GGS2_HOLD:
            mode = GGS2
        elif event == APB1_HOLD:
            mode = APB1
        elif event == APB2_HOLD:
            mode = APB2
        if mode_is_active(mode):
            # Повторный запрос не меняет владельца.
            return
        # Сначала проверяем все конфликты, только потом меняем состояние.
        # RECEIVE можно вытеснить; каналы ANNOUNCE проверяются отдельно ниже.
        for active in range(MODE_COUNT):
            if (mode_is_active(active) and active != RECEIVE and active != RECORD
                    and not modes_are_compatible(mode, active)):
                print("  " + app_mode_t(mode).name + ": запрос", source.name,
                      "отклонён — активен", app_mode_t(active).name)
                return
        if mode_is_active(RECEIVE):
            print("  RECEIVE: вытеснен режимом", app_mode_t(mode).name)
            end_receive()
        channel = CHANNEL_1
        if mode == GGS2 or mode == APB2:
            channel = CHANNEL_2
        if mode_is_active(ANNOUNCE) and (state.announce_channels & channel) != 0:
            remaining_channels = state.announce_channels & ~channel
            if ANNOUNCE_REQUIRE_ALL_CHANNELS or remaining_channels == 0:
                print("  ANNOUNCE: вытеснен режимом", app_mode_t(mode).name)
                end_announce()
            else:
                set_announce_channels(remaining_channels)
        if mode_is_active(PHRASE) and state.phrase_channel == channel:
            print("  PHRASE: вытеснена режимом", app_mode_t(mode).name)
            end_phrase()
        if mode_is_active(RECORD):
            print("  RECORD: вытеснена режимом", app_mode_t(mode).name)
            end_record(True)
        start_transmission(mode, source)

    elif event in (GGS1_RELEASE, GGS2_RELEASE, APB1_RELEASE, APB2_RELEASE):
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if event == GGS1_RELEASE:
            mode = GGS1
        elif event == GGS2_RELEASE:
            mode = GGS2
        elif event == APB1_RELEASE:
            mode = APB1
        elif event == APB2_RELEASE:
            mode = APB2
        if mode_is_active(mode) and state.modes[mode].source == source:
            end_transmission(mode)

    elif event == AUTO_ANNOUNCE_1_TOGGLE or event == AUTO_ANNOUNCE_2_TOGGLE:
        # Изменение состава автоматики прерывает только автоматическое оповещение.
        if mode_is_active(ANNOUNCE) and state.modes[ANNOUNCE].source == OUTSOURCE:
            end_announce()
        if event == AUTO_ANNOUNCE_1_TOGGLE:
            set_auto_announce_1(not state.auto_announce_1)
        else:
            set_auto_announce_2(not state.auto_announce_2)
        reset_auto_announce_counters()

    elif event in (ANNOUNCE_START, ANNOUNCE_1_HOLD, ANNOUNCE_2_HOLD):
        button = -1
        if event == ANNOUNCE_START:
            if source != OUTSOURCE or not (state.auto_announce_1 or state.auto_announce_2):
                return
        else:
            if source != REMOTE_1 and source != REMOTE_2:
                return
            if event == ANNOUNCE_1_HOLD:
                button = B_F1
            else:
                button = B_F2
        if event != ANNOUNCE_START and state.unlock_record:
            if mode_is_active(RECORD):
                return
            if (mode_is_active(ALERT) or mode_is_active(ACHTUNG)
                    or mode_is_active(RECEIVE) or mode_is_active(GGS1)
                    or mode_is_active(GGS2) or mode_is_active(APB1)
                    or mode_is_active(APB2)):
                print("  RECORD: запрос отклонён — активен более приоритетный режим")
                return
            if mode_is_active(ANNOUNCE):
                print("  ANNOUNCE: вытеснен режимом RECORD")
                end_announce()
            if mode_is_active(PHRASE):
                print("  PHRASE: вытеснена режимом RECORD")
                end_phrase()
            start_record(source, button)
            return
        if mode_is_active(RECORD):
            print("  ANNOUNCE: запрос отклонён — активна запись")
            return
        if mode_is_active(ANNOUNCE):
            return
        if mode_is_active(ALERT) or mode_is_active(ACHTUNG):
            print("  ANNOUNCE: запрос отклонён — активен ALERT или ACHTUNG")
            return
        requested_channels = ANNOUNCE_CHANNELS & (CHANNEL_1 | CHANNEL_2)
        channels = requested_channels
        if mode_is_active(GGS1) or mode_is_active(APB1):
            channels &= ~CHANNEL_1
        if mode_is_active(GGS2) or mode_is_active(APB2):
            channels &= ~CHANNEL_2
        if mode_is_active(PHRASE):
            channels &= ~state.phrase_channel
        if (channels == 0
                or (ANNOUNCE_REQUIRE_ALL_CHANNELS and channels != requested_channels)):
            print("  ANNOUNCE: запрос отклонён — нужные каналы недоступны")
            return
        start_announce(channels, source, button)

    elif event == ANNOUNCE_1_RELEASE or event == ANNOUNCE_2_RELEASE:
        if source != REMOTE_1 and source != REMOTE_2:
            return
        button = B_F1
        if event == ANNOUNCE_2_RELEASE:
            button = B_F2
        if mode_is_active(RECORD):
            if state.modes[RECORD].source == source and state.record_button == button:
                end_record(state.record_invalid)
            return
        if not mode_is_active(ANNOUNCE):
            return
        if (state.modes[ANNOUNCE].source == OUTSOURCE
                or (state.modes[ANNOUNCE].source == source
                    and state.modes[ANNOUNCE].is_holding
                    and state.announce_button == button)):
            end_announce()

    elif event == ANNOUNCE_FINISHED:
        if (source == OUTSOURCE and mode_is_active(ANNOUNCE)
                and state.modes[ANNOUNCE].source == OUTSOURCE):
            print("  ANNOUNCE: обработчик завершил оповещение")
            end_announce()

    elif event in (
            PHRASE_1_PRM, PHRASE_1_GGS1, PHRASE_1_GGS2, PHRASE_1_APB1, PHRASE_1_APB2,
            PHRASE_2_PRM, PHRASE_2_GGS1, PHRASE_2_GGS2, PHRASE_2_APB1, PHRASE_2_APB2):
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if mode_is_active(RECORD):
            print("  PHRASE: запрос отклонён — активна запись")
            return
        if mode_is_active(PHRASE):
            print("  PHRASE: запрос", source.name, "отклонён — другая фраза уже воспроизводится")
            return
        if mode_is_active(ALERT) or mode_is_active(ACHTUNG):
            print("  PHRASE: запрос отклонён — активен ALERT или ACHTUNG")
            return
        if event == PHRASE_1_PRM:
            number = 1
            output = RECEIVE
            channel = CHANNEL_PRM
        elif event == PHRASE_1_GGS1:
            number = 1
            output = GGS1
            channel = CHANNEL_1
        elif event == PHRASE_1_GGS2:
            number = 1
            output = GGS2
            channel = CHANNEL_2
        elif event == PHRASE_1_APB1:
            number = 1
            output = APB1
            channel = CHANNEL_1
        elif event == PHRASE_1_APB2:
            number = 1
            output = APB2
            channel = CHANNEL_2
        elif event == PHRASE_2_PRM:
            number = 2
            output = RECEIVE
            channel = CHANNEL_PRM
        elif event == PHRASE_2_GGS1:
            number = 2
            output = GGS1
            channel = CHANNEL_1
        elif event == PHRASE_2_GGS2:
            number = 2
            output = GGS2
            channel = CHANNEL_2
        elif event == PHRASE_2_APB1:
            number = 2
            output = APB1
            channel = CHANNEL_1
        elif event == PHRASE_2_APB2:
            number = 2
            output = APB2
            channel = CHANNEL_2
        if ((channel == CHANNEL_PRM and mode_is_active(RECEIVE))
                or (channel == CHANNEL_1 and (mode_is_active(GGS1) or mode_is_active(APB1)))
                or (channel == CHANNEL_2 and (mode_is_active(GGS2) or mode_is_active(APB2)))):
            print("  PHRASE: запрос отклонён — выбранный канал занят")
            return
        if mode_is_active(ANNOUNCE) and (state.announce_channels & channel) != 0:
            remaining_channels = state.announce_channels & ~channel
            if ANNOUNCE_REQUIRE_ALL_CHANNELS or remaining_channels == 0:
                print("  ANNOUNCE: вытеснен режимом PHRASE")
                end_announce()
            else:
                set_announce_channels(remaining_channels)
        start_phrase(source, number, output, channel)

    elif event == PHRASE_FINISHED:
        if source == OUTSOURCE and mode_is_active(PHRASE):
            print("  PHRASE: обработчик завершил воспроизведение")
            end_phrase()

    elif event == VOLUME_UP or event == VOLUME_DOWN:
        if source != REMOTE_1 and source != REMOTE_2:
            return
        if button == B_GGS:
            mode = GGS1
            target = volume_target_t.GGS1
        elif button == B_GGS2:
            mode = GGS2
            target = volume_target_t.GGS2
        elif button == B_APB:
            mode = APB1
            target = volume_target_t.APB1
        elif button == B_PRD:
            mode = APB2
            target = volume_target_t.APB2
        elif button == B_PRM:
            mode = RECEIVE
            target = volume_target_t.RECEIVE
        elif button == B_F1 or button == B_F2:
            if mode_is_active(RECORD):
                mode = RECORD
                if button != state.record_button:
                    return
                target = volume_target_t.PHRASE_1
                if button == B_F2:
                    target = volume_target_t.PHRASE_2
            else:
                mode = ANNOUNCE
                if button != state.announce_button:
                    return
                target = volume_target_t.ANNOUNCE
        else:
            return
        if (not mode_is_active(mode) or state.modes[mode].source != source
                or not state.modes[mode].is_holding):
            return
        if mode == RECORD and not state.record_invalid:
            invalidate_record()
        value = state.volume[target]
        if event == VOLUME_UP:
            value += VOLUME_STEP
        else:
            value -= VOLUME_STEP
        if value < VOLUME_MIN:
            value = VOLUME_MIN
        if value > VOLUME_MAX:
            value = VOLUME_MAX
        set_volume(target, value)

    # TRK_RELEASED не обрабатывается: таймаут проверяет process_auto_receive().


## Периодическое обслуживание активных режимов
def process_state_logic():
    if mode_is_active(ALERT):
        process_alert()
    if mode_is_active(ACHTUNG):
        process_achtung()
    if mode_is_active(RECEIVE):
        if state.modes[RECEIVE].source == OUTSOURCE:
            result = process_auto_receive()
            if result == FINISHED:
                process_state_event(RECEIVE_FINISHED, OUTSOURCE)
        else:
            process_receive()

    if mode_is_active(ANNOUNCE):
        if state.modes[ANNOUNCE].source == OUTSOURCE:
            if process_announce() == FINISHED:
                process_state_event(ANNOUNCE_FINISHED, OUTSOURCE)
        else:
            process_manual_announce()

    if mode_is_active(PHRASE):
        if process_phrase() == FINISHED:
            process_state_event(PHRASE_FINISHED, OUTSOURCE)

    if mode_is_active(RECORD):
        process_record()
    if state.unlock_record:
        if process_record_unlock() == FINISHED:
            process_state_event(RECORD_UNLOCK_EXPIRED, OUTSOURCE)

    # Обслуживаем только один обработчик автоматики за цикл.
    # Новый ANNOUNCE начнёт периодическую обработку со следующего цикла.
    result = FINISHED
    if state.auto_announce_1 and state.auto_announce_2:
        result = process_auto_announce_3()
    elif state.auto_announce_1:
        result = process_auto_announce_1()
    elif state.auto_announce_2:
        result = process_auto_announce_2()
    if result == PROCESSING:
        process_state_event(ANNOUNCE_START, OUTSOURCE)


# Цикл тестирования эмулятора
def loop():
    for cycle in range(TESTING_CYCLES):
        print("Цикл", cycle + 1)
        process_achtung_button()
        process_trk_button()
        process_button_combinations()
        process_state_logic()
        print("  UNLOCK_RECORD:", state.unlock_record)
        print("  Автоматика AUTO_RECEIVE:", state.auto_receive)
        print("  Автоматика AUTO_ANNOUNCE_1:", state.auto_announce_1,
              "AUTO_ANNOUNCE_2:", state.auto_announce_2)
        if mode_is_active(ANNOUNCE):
            print("  ANNOUNCE: маска каналов", state.announce_channels)
        if state.active_modes == 0:
            print("  Состояние: IDLE")
        else:
            for mode in range(MODE_COUNT):
                if mode_is_active(mode):
                    print("  Состояние:", app_mode_t(mode).name,
                          "источник", state.modes[mode].source.name)


if __name__ == "__main__":
    loop()