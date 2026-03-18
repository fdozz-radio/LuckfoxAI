#!/bin/bash
# Скрипт настройки окружения для LuckFox Pico SDK
# Использование: source env.sh [путь_к_sdk]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Если путь к SDK не указан, пробуем найти автоматически
if [ -z "$1" ]; then
    echo "Поиск SDK LuckFox..."
    
    # Пробуем найти в домашних директориях
    SDK_CANDIDATES=$(find /home -type d -name "luckfox-pico" 2>/dev/null | head -5)
    
    if [ -z "$SDK_CANDIDATES" ]; then
        # Если не найдено luckfox-pico, ищем по наличию toolchain
        TOOLCHAIN_PATH=$(find /home -type d -name "arm-rockchip830-linux-uclibcgnueabihf" 2>/dev/null | head -1)
        if [ -n "$TOOLCHAIN_PATH" ]; then
            SDK_PATH=$(dirname $(dirname $(dirname $(dirname "$TOOLCHAIN_PATH"))))
            echo "Найден SDK по тулчейну: $SDK_PATH"
        else
            echo "Ошибка: SDK LuckFox не найден!"
            echo "Укажите путь к SDK явно: source env.sh /путь/к/luckfox-pico"
            echo ""
            echo "Или скачайте SDK:"
            echo "  git clone https://github.com/LuckfoxTECH/luckfox-pico.git"
            return 1
        fi
    else
        # Выбираем первый найденный вариант
        SDK_PATH=$(echo "$SDK_CANDIDATES" | head -1)
        echo "Найден SDK: $SDK_PATH"
    fi
else
    SDK_PATH="$1"
    echo "Используем указанный SDK: $SDK_PATH"
fi

# Проверяем существование SDK
if [ ! -d "$SDK_PATH" ]; then
    echo "Ошибка: Директория $SDK_PATH не существует!"
    return 1
fi

# Поиск тулчейна
TOOLCHAIN_DIR=""
if [ -d "$SDK_PATH/toolchain/gcc/linux-x86_64/arm-rockchip830-linux-uclibcgnueabihf/bin" ]; then
    TOOLCHAIN_DIR="$SDK_PATH/toolchain/gcc/linux-x86_64/arm-rockchip830-linux-uclibcgnueabihf/bin"
else
    # Ищем альтернативный путь
    FOUND_TOOLCHAIN=$(find "$SDK_PATH" -name "arm-rockchip830-linux-uclibcgnueabihf-g++" 2>/dev/null | head -1)
    if [ -n "$FOUND_TOOLCHAIN" ]; then
        TOOLCHAIN_DIR=$(dirname "$FOUND_TOOLCHAIN")
    fi
fi

if [ -z "$TOOLCHAIN_DIR" ]; then
    echo "Ошибка: Тулчейн не найден в $SDK_PATH"
    return 1
fi

echo "Тулчейн: $TOOLCHAIN_DIR"

# Добавляем тулчейн в PATH
export PATH="$TOOLCHAIN_DIR:$PATH"

# Экспортируем переменные для Makefile
export SDK_PATH
export TOOLCHAIN_PATH="$TOOLCHAIN_DIR"

echo ""
echo "=========================================="
echo "Окружение LuckFox настроено!"
echo "=========================================="
echo "SDK_PATH: $SDK_PATH"
echo "TOOLCHAIN_PATH: $TOOLCHAIN_PATH"
echo ""
echo "Теперь вы можете выполнить:"
echo "  make clean && make"
echo ""
echo "Проверка компилятора:"
which arm-rockchip830-linux-uclibcgnueabihf-g++
echo "=========================================="
