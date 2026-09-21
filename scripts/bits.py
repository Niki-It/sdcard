#!/usr/bin/env python3
import sys


SIZES = [8, 16, 32, 64]


def parse_number(value: str) -> int:
    """Поддержка 0x..., 0b..., 0o... и обычных десятичных чисел."""
    value = value.strip().replace("_", "")

    if not value:
        raise ValueError("Пустое значение")

    try:
        return int(value, 0)
    except ValueError:
        # На случай обычного числа без префикса
        try:
            return int(value, 10)
        except ValueError:
            raise ValueError(f"Некорректное число: {value}")


def select_size() -> int:
    """Интерактивный выбор размера стрелками."""
    selected = 0

    # Windows
    if sys.platform == "win32":
        import msvcrt

        while True:
            print("\rРазмер: ", end="")

            for i, size in enumerate(SIZES):
                if i == selected:
                    print(f"\033[7m {size} \033[0m", end="")
                else:
                    print(f" {size} ", end="")

            print("   ← →, Enter", end="", flush=True)

            key = msvcrt.getch()

            if key in (b"\x00", b"\xe0"):
                key = msvcrt.getch()

                if key == b"K":       # Left
                    selected = (selected - 1) % len(SIZES)
                elif key == b"M":     # Right
                    selected = (selected + 1) % len(SIZES)

            elif key in (b"\r", b"\n"):
                print()
                return SIZES[selected]

            elif key == b"\x1b":
                print("\nОтмена.")
                sys.exit(0)

    # Linux / macOS
    else:
        import termios
        import tty

        old_settings = termios.tcgetattr(sys.stdin)

        try:
            tty.setraw(sys.stdin.fileno())

            while True:
                # Очищаем текущую строку
                print("\r\033[KРазмер: ", end="")

                for i, size in enumerate(SIZES):
                    if i == selected:
                        print(f"\033[7m {size} \033[0m", end="")
                    else:
                        print(f" {size} ", end="")

                print("   ← →, Enter", end="", flush=True)

                key = sys.stdin.read(1)

                if key == "\x1b":
                    # Escape sequence стрелок: ESC [ D / C
                    next1 = sys.stdin.read(1)

                    if next1 == "[":
                        next2 = sys.stdin.read(1)

                        if next2 == "D":       # Left
                            selected = (selected - 1) % len(SIZES)
                        elif next2 == "C":     # Right
                            selected = (selected + 1) % len(SIZES)
                    else:
                        print("\nОтмена.")
                        sys.exit(0)

                elif key in ("\r", "\n"):
                    print()
                    return SIZES[selected]

        finally:
            termios.tcsetattr(
                sys.stdin,
                termios.TCSADRAIN,
                old_settings
            )


def print_bits(value: int, size: int):
    """Вывод таблицы битов."""
    print()
    print(f"  Значение: {value} (0x{value:X})")
    print(f"  Размер:   {size} бит")
    print()

    # Заголовок
    print("  Bit:  ", end="")
    for bit in range(size - 1, -1, -1):
        print(f"{bit:>3}", end="")
    print()

    print("        " + "---" * size)

    # Значения битов
    print("  Val:  ", end="")
    for bit in range(size - 1, -1, -1):
        print(f"{(value >> bit) & 1:>3}", end="")
    print()

    print()

    # Более удобный вариант: бит -> значение
    print("  Биты:")
    for start in range(size - 1, -1, -8):
        end = max(0, start - 7)

        items = []
        for bit in range(start, end - 1, -1):
            bit_value = (value >> bit) & 1
            items.append(f"bit {bit:>2} = {bit_value}")

        print("    " + "   ".join(items))

    print()


def main():
    print("=" * 60)
    print("              BIT VIEWER")
    print("=" * 60)
    print()
    print("Введите число:")
    print("  Например: 0xFF, 255, 0b10101010, 0x1234")
    print()

    while True:
        try:
            raw = input("> ").strip()

            if raw.lower() in ("q", "quit", "exit"):
                break

            value = parse_number(raw)

            if value < 0:
                print("Ошибка: отрицательные числа пока не поддерживаются.")
                continue

            size = select_size()

            max_value = (1 << size) - 1

            if value > max_value:
                print()
                print(
                    f"Ошибка: число 0x{value:X} не помещается "
                    f"в {size} бит."
                )
                print(
                    f"Максимальное значение для {size} бит: "
                    f"0x{max_value:X}"
                )
                print()
                continue

            print_bits(value, size)

            print("-" * 60)
            print()

        except KeyboardInterrupt:
            print("\n")
            break

        except Exception as e:
            print(f"\nОшибка: {e}\n")


if __name__ == "__main__":
    main()