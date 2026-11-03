
"""STM32 remote emulator: 10 button event codes via COM4, 38400 8N1.

Requires: pip install pyserial
Run:      python remote_emulator.py
"""

import queue
import threading
import time
import tkinter as tk

import serial


PORT = "COM4"
BAUDRATE = 38400
SERIAL_TIMEOUT = 0.05
RECONNECT_DELAY = 1.0

NO_EVENT = 0
SHORT_EVENT = 1
DOUBLE_SHORT = 2
LONG_EVENT = 3

EVENT_CHOICES = (
    ("—", NO_EVENT),
    ("SHORT", SHORT_EVENT),
    ("DOUBLE", DOUBLE_SHORT),
    ("LONG", LONG_EVENT),
)

BUTTON_NAMES = ("!", "ПРМ", "ГГС1", "Ф1", "ГГС2", "Ф2", "АПБ", "+", "−", "ПРД")

BG = "#111827"
PANEL = "#1F2937"
BUTTON = "#374151"
SELECTED = "#2563EB"
ACTIVE_LONG = "#15803D"
TEXT = "#F9FAFB"
MUTED = "#9CA3AF"


def crc8(data: bytes) -> int:
    """Byte-for-byte equivalent of the supplied C update_crc()/crc()."""
    result = 0

    for byte in data:
        for _ in range(8):
            if (byte ^ result) & 0x80:
                result = ((result << 1) ^ 0xA9) & 0xFF
            else:
                result = (result << 1) & 0xFF

            byte = (byte << 1) & 0xFF

    return result


def make_response(request: bytes, events: list[int]) -> bytes:
    """4-byte reply: command/frame, S1..S10 (2 bits each), CRC."""
    # Byte 1: command bit 7; S10 bits 5..6; S9 bits 3..4; frame bits 0..1.
    b0 = (request[0] & 0x83) | (events[8] << 3) | (events[9] << 5)

    # Byte 2: S5..S8, 2 bits per button.
    b1 = events[4] | (events[5] << 2) | (events[6] << 4) | (events[7] << 6)

    # Byte 3: S1..S4, 2 bits per button.
    b2 = events[0] | (events[1] << 2) | (events[2] << 4) | (events[3] << 6)

    payload = bytes((b0, b1, b2))
    return payload + bytes((crc8(payload),))


class EventState:
    def __init__(self):
        self.lock = threading.Lock()
        self.long_enabled = [False] * 10
        self.oneshot = [NO_EVENT] * 10
        self.version = 0
        self.rx_bytes = 0

    def add_rx_bytes(self, count: int) -> None:
        with self.lock:
            self.rx_bytes += count

    def get_rx_bytes(self) -> int:
        with self.lock:
            return self.rx_bytes

    def save(self, selection: list[int]) -> None:
        with self.lock:
            self.long_enabled = [value == LONG_EVENT for value in selection]
            self.oneshot = [
                value if value in (SHORT_EVENT, DOUBLE_SHORT) else NO_EVENT
                for value in selection
            ]
            self.version += 1

    def snapshot(self) -> tuple[list[int], int]:
        with self.lock:
            events = [
                LONG_EVENT if active else self.oneshot[i]
                for i, active in enumerate(self.long_enabled)
            ]
            return events, self.version

    def mark_sent(self, version: int) -> None:
        # A later Save may have installed new events; don't clear those.
        with self.lock:
            if self.version == version:
                self.oneshot = [NO_EVENT] * 10


def serial_worker(state: EventState, updates: queue.Queue, stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            with serial.Serial(
                port=PORT,
                baudrate=BAUDRATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=SERIAL_TIMEOUT,
                write_timeout=0.2,
            ) as port:
                updates.put((True, f"Подключено: {PORT} · {BAUDRATE} · 8N1"))

                rx = bytearray()
                echo = bytearray()
                echo_deadline = 0.0

                while not stop.is_set():
                    incoming = port.read(1)

                    if not incoming:
                        continue

                    byte = incoming[0]

                    # Игнорирование собственного эха TX
                    if echo:
                        if time.monotonic() > echo_deadline:
                            echo.clear()
                        elif byte == echo[0]:
                            del echo[0]
                            continue
                        else:
                            echo.clear()

                    state.add_rx_bytes(1)
                    rx.append(byte)

                    while len(rx) >= 3:
                        request = bytes(rx[:3])

                        if crc8(request[:2]) != request[2]:
                            del rx[0]
                            continue

                        del rx[:3]

                        if request == b"\x00\x00\x00":
                            continue

                        events, version = state.snapshot()
                        response = make_response(request, events)

                        port.write(response)

                        # Запоминаем отправленные байты,
                        # чтобы не принимать их как новый запрос.
                        echo = bytearray(response)
                        echo_deadline = time.monotonic() + 0.003

                        state.mark_sent(version)

        except (serial.SerialException, OSError) as exc:
            if not stop.is_set():
                updates.put((False, f"{PORT}: {exc}"))
                stop.wait(RECONNECT_DELAY)


class RemoteEmulatorUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Remote Emulator · STM32")
        self.root.configure(bg=BG)
        self.root.resizable(False, False)

        self.state = EventState()
        self.updates = queue.Queue()
        self.stop = threading.Event()
        self.choices = [NO_EVENT] * 10
        self.button_widgets: list[list[tk.Button]] = []

        wrapper = tk.Frame(self.root, bg=BG, padx=22, pady=18)
        wrapper.pack()

        tk.Label(
            wrapper,
            text="Remote Emulator",
            bg=BG,
            fg=TEXT,
            font=("Segoe UI", 19, "bold"),
        ).pack(anchor="w")

        tk.Label(
            wrapper,
            text="S1–S10 · одно событие на кнопку · изменения после Save",
            bg=BG,
            fg=MUTED,
            font=("Segoe UI", 10),
        ).pack(anchor="w", pady=(4, 15))

        panel = tk.Frame(wrapper, bg=PANEL, padx=12, pady=12)
        panel.pack(fill="x")

        for index, name in enumerate(BUTTON_NAMES):
            row = tk.Frame(panel, bg=PANEL)
            row.pack(fill="x", pady=3)

            tk.Label(
                row,
                text=f"S{index + 1}",
                bg=PANEL,
                fg=TEXT,
                font=("Segoe UI", 11, "bold"),
                width=4,
                anchor="w",
            ).pack(side="left")

            tk.Label(
                row,
                text=name,
                bg=PANEL,
                fg=MUTED,
                font=("Segoe UI", 10),
                width=6,
                anchor="w",
            ).pack(side="left", padx=(0, 8))

            buttons = []

            for label, value in EVENT_CHOICES:
                widget = tk.Button(
                    row,
                    text=label,
                    width=9,
                    height=1,
                    font=("Segoe UI", 9),
                    fg=TEXT,
                    bg=BUTTON,
                    activeforeground=TEXT,
                    activebackground=SELECTED,
                    relief="flat",
                    bd=0,
                    cursor="hand2",
                    command=lambda i=index, v=value: self.select(i, v),
                )

                widget.pack(side="left", padx=2, ipadx=3, ipady=4)
                buttons.append(widget)

            self.button_widgets.append(buttons)

        self.refresh_choices()

        self.save_button = tk.Button(
            wrapper,
            text="Save",
            bg=SELECTED,
            fg=TEXT,
            activebackground="#1D4ED8",
            activeforeground=TEXT,
            font=("Segoe UI", 12, "bold"),
            relief="flat",
            bd=0,
            command=self.save,
            cursor="hand2",
        )
        self.save_button.pack(fill="x", pady=(16, 8), ipady=9)

        self.note = tk.Label(
            wrapper,
            text="SHORT/DOUBLE: один ответ · LONG: каждый ответ до отмены",
            bg=BG,
            fg=MUTED,
            font=("Segoe UI", 9),
        )
        self.note.pack(anchor="w", pady=(1, 15))

        self.connection = tk.Label(
            wrapper,
            text=f"Подключение к {PORT}...",
            bg=BG,
            fg=MUTED,
            anchor="w",
            justify="left",
            wraplength=560,
            font=("Segoe UI", 9),
        )
        self.connection.pack(fill="x")

        self.rx_counter = tk.Label(
            wrapper,
            text="Принято байтов: 0",
            bg=BG,
            fg=MUTED,
            anchor="w",
            font=("Segoe UI", 10),
        )
        self.rx_counter.pack(fill="x", pady=(8, 0))

        self.worker = threading.Thread(
            target=serial_worker,
            args=(self.state, self.updates, self.stop),
            daemon=True,
        )
        self.worker.start()

        self.root.after(100, self.update_connection)
        self.root.after(1000, self.update_rx_counter)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def select(self, index: int, event: int) -> None:
        self.choices[index] = event
        self.refresh_choices()

    def refresh_choices(self) -> None:
        for index, row in enumerate(self.button_widgets):
            for event, widget in enumerate(row):
                selected = self.choices[index] == event

                bg = (
                    ACTIVE_LONG if event == LONG_EVENT else SELECTED
                ) if selected else BUTTON

                if selected and event == NO_EVENT:
                    bg = "#4B5563"

                widget.configure(bg=bg, activebackground=bg)

    def save(self) -> None:
        self.state.save(self.choices)

        # Keep active LONG visibly selected; reset SHORT/DOUBLE to —.
        self.choices = [
            LONG_EVENT if value == LONG_EVENT else NO_EVENT
            for value in self.choices
        ]

        self.refresh_choices()
        self.note.configure(
            text="Сохранено. SHORT/DOUBLE сброшены; LONG остаётся активным."
        )

    def update_connection(self) -> None:
        try:
            while True:
                connected, message = self.updates.get_nowait()

                self.connection.configure(
                    text=message,
                    fg="#4ADE80" if connected else "#FCA5A5",
                )
        except queue.Empty:
            pass

        if not self.stop.is_set():
            self.root.after(100, self.update_connection)

    def update_rx_counter(self) -> None:
        self.rx_counter.configure(
            text=f"Принято байтов: {self.state.get_rx_bytes()}"
        )

        if not self.stop.is_set():
            self.root.after(1000, self.update_rx_counter)

    def close(self) -> None:
        self.stop.set()
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


if __name__ == "__main__":
    RemoteEmulatorUI().run()
