import serial
import threading
import sys
import os

# ============================================================
#  НАСТРОЙКИ — МЕНЯЙТЕ ЗДЕСЬ
# ============================================================
PORT     = 'COM4'
BAUDRATE = 115200
MODE     = 'HEX'   # 'HEX' или 'DEC'
# ============================================================

ser = None
stop_event = threading.Event()

# ---------- Чтение (Слушатель) ----------
def reader_thread_func():
    try:
        while not stop_event.is_set():
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                if data:
                    parts = []
                    for b in data:
                        ascii_char = chr(b) if 32 <= b < 127 else '.'
                        if MODE == 'HEX':
                            parts.append(f"{b:02X} ({ascii_char})")
                        else:
                            parts.append(f"{b:03d} ({ascii_char})")

                    print(f"\nRX [{len(data)} bytes]: {' '.join(parts)}")
                    print("TX > ", end='', flush=True)

            if ser.in_waiting == 0:
                stop_event.wait(0.05)
    except Exception as e:
        if not stop_event.is_set():
            print(f"[-] Ошибка чтения: {e}")

# ---------- Запись (Отправитель) ----------
def writer_thread_func():
    try:
        while not stop_event.is_set():
            try:
                line = input("TX > ")
            except EOFError:
                break

            if not line.strip():
                continue

            if line.strip().lower() in ('q', 'exit', 'quit'):
                print("\n[*] Команда выхода. Завершение...")
                stop_event.set()
                break

            try:
                tokens = line.strip().split()
                byte_list = []

                for t in tokens:
                    if MODE == 'HEX':
                        # Убираем возможный префикс 0x
                        clean = t.lower().replace('0x', '')
                        num = int(clean, 16)
                    else:
                        num = int(t, 10)

                    if 0 <= num <= 255:
                        byte_list.append(num)
                    else:
                        raise ValueError(f"{t} -> {num} вне диапазона 0-255")

                ser.write(bytes(byte_list))

            except ValueError as e:
                if MODE == 'HEX':
                    hint = "Вводите HEX числа через пробел (пример: 4A FF 00)"
                else:
                    hint = "Вводите DEC числа через пробел (пример: 65 255 0)"
                print(f"[-] Ошибка: {e}")
                print(f"    {hint}")
                print("TX > ", end='', flush=True)
            except Exception as e:
                print(f"[-] Ошибка отправки: {e}")

    except Exception as e:
        print(f"[-] Ошибка ввода: {e}")

# ---------- Основная функция ----------
def main():
    global ser

    if MODE not in ('HEX', 'DEC'):
        print(f"[-] Неизвестный MODE='{MODE}'. Используйте 'HEX' или 'DEC'.")
        return

    try:
        ser = serial.Serial(
            port=PORT,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.1
        )

        print(f"[+] Порт {PORT} открыт  |  {BAUDRATE} 8N1  |  Режим: {MODE}")

        if MODE == 'HEX':
            print("[*] Ввод: 48 65 6C 6C 6F  (HEX через пробел)")
        else:
            print("[*] Ввод: 72 101 108 108 111  (DEC через пробел)")

        print("[*] Выход: q + Enter")
        print("-" * 50)

        t_reader = threading.Thread(target=reader_thread_func, daemon=True)
        t_reader.start()

        writer_thread_func()

    except serial.SerialException as e:
        print(f"[-] Не удалось открыть порт {PORT}: {e}")
    except KeyboardInterrupt:
        print("\n[*] Прервано пользователем.")
    finally:
        stop_event.set()
        if ser and ser.is_open:
            ser.close()
            print("[+] Порт закрыт.")

if __name__ == "__main__":
    main()