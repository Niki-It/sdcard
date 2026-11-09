#!/usr/bin/env python3
"""
Генератор sources.cmake для смешанного C/C++ embedded-проекта.
Рекурсивно собирает исходники из указанных директорий.
"""

from pathlib import Path

# ====== НАСТРОЙКИ ======
DIRS = [
    "app",
    "core",
    "src",
    "drivers",
]

SOURCE_EXTENSIONS = {".c", ".cpp", ".cc", ".cxx"}
VAR_NAME = "SOURCES"
OUTPUT_FILE = "sources.cmake"  # None = вывод в консоль
# =======================


def collect_source_files(directories: list[str], base_dir: Path) -> list[str]:
    """Рекурсивно собирает C/C++ файлы в стабильном порядке."""
    sources: list[str] = []
    seen: set[str] = set()

    for directory in directories:
        dir_path = base_dir / directory
        if not dir_path.is_dir():
            print(f"[WARN] Директория не найдена: {dir_path}")
            continue

        group = []
        for path in dir_path.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_EXTENSIONS:
                rel_path = path.relative_to(base_dir).as_posix()
                if rel_path not in seen:
                    seen.add(rel_path)
                    group.append(rel_path)

        sources.extend(sorted(group))

    return sources


def generate_cmake_sources(sources: list[str], variable: str = "SOURCES") -> str:
    """Формирует содержимое set(SOURCES ...) для CMake."""
    lines = [f"set({variable}"]
    for src in sources:
        lines.append(f'    "{src}"')
    lines.append(")")
    return "\n".join(lines) + "\n"


def main() -> int:
    # Предполагаем, что скрипт расположен в корне проекта.
    base_dir = Path(__file__).resolve().parent.parent
    sources = collect_source_files(DIRS, base_dir)

    if not sources:
        print("[ERROR] Не найдено ни одного C/C++ исходника!")
        return 1

    c_count = sum(Path(src).suffix == ".c" for src in sources)
    cpp_count = len(sources) - c_count
    print(f"[INFO] Найдено {len(sources)} файлов: C={c_count}, C++={cpp_count}")

    result = generate_cmake_sources(sources, VAR_NAME)

    if OUTPUT_FILE is None:
        print(result, end="")
        return 0

    output_path = base_dir / OUTPUT_FILE
    if output_path.is_file() and output_path.read_text(encoding="utf-8") == result:
        print(f"[OK] Без изменений: {output_path}")
        return 0

    output_path.write_text(result, encoding="utf-8")
    print(f"[OK] Результат записан в {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
