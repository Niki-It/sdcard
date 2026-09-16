#!/usr/bin/env python3
"""
Генератор CMake SOURCES для embedded-проекта.
Рекурсивно собирает .c файлы из указанных папок и пишет sources.cmake.
"""

import os
from pathlib import Path

# ====== НАСТРОЙКИ ======
# Папки, в которых ищем .c файлы (порядок сохраняется в выводе)
DIRS = [
    "app",
    "core",
    "src",
    "drivers",
]

# Имя CMake-переменной
VAR_NAME = "SOURCES"

# Файл для записи результата (None = вывод в консоль)
OUTPUT_FILE = "sources.cmake"
# =======================


def collect_c_files(directories: list[str], base_dir: str = ".") -> list[str]:
    """Рекурсивно собирает все .c файлы из указанных директорий."""
    sources = []
    base = Path(base_dir).resolve()

    for directory in directories:
        dir_path = base / directory
        if not dir_path.exists():
            print(f"[WARN] Директория не найдена: {dir_path}")
            continue

        group = []
        for root, _, files in os.walk(dir_path):
            for file in files:
                if file.endswith(".c"):
                    rel_path = Path(root, file).relative_to(base).as_posix()
                    group.append(rel_path)

        # Сортируем файлы внутри каждой папки для стабильного вывода
        group.sort()
        sources.extend(group)

    return sources


def generate_cmake_sources(sources: list[str], variable: str = "SOURCES") -> str:
    """Формирует строку set(SOURCES ...) для CMakeLists.txt."""
    lines = [f"set({variable}"]
    for src in sources:
        lines.append(f'    "{src}"')
    lines.append(")")
    return "\n".join(lines)


def main():
    sources = collect_c_files(DIRS)

    if not sources:
        print("[ERROR] Не найдено ни одного .c файла!")
        return

    print(f"[INFO] Найдено {len(sources)} .c файл(ов)")

    result = generate_cmake_sources(sources, VAR_NAME)

    if OUTPUT_FILE:
        Path(OUTPUT_FILE).write_text(result, encoding="utf-8")
        print(f"[OK] Результат записан в {OUTPUT_FILE}")
    else:
        print("\n" + "=" * 60)
        print(result)
        print("=" * 60)


if __name__ == "__main__":
    main()