#!/bin/bash
set -e

# Проверка прав root
if [ "$EUID" -ne 0 ]; then
    echo "Пожалуйста, запустите с sudo: sudo $0"
    exit 1
fi

# Определение директории скрипта
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Определение домашней директории пользователя
if [ -n "$SUDO_USER" ]; then
    USER_HOME=$(getent passwd $SUDO_USER | cut -d: -f6)
else
    USER_HOME=$HOME
fi

# Директория для драйверов (внутри проекта)
DRIVERS_DIR="$SCRIPT_DIR/drivers"
REPO_DIR="$DRIVERS_DIR/tuxedo-drivers"
REPO_URL="https://github.com/tuxedocomputers/tuxedo-drivers.git"

echo "=== Tuxedo Drivers Installer/Updater ==="
echo ""

# Создание директории для драйверов
mkdir -p "$DRIVERS_DIR"

# Клонирование или обновление репозитория
cd "$DRIVERS_DIR"
if [ -d "$REPO_DIR" ]; then
    echo "✓ Обновление репозитория в $REPO_DIR..."
    cd "$REPO_DIR"
    if [ -n "$SUDO_USER" ]; then
        sudo -u "$SUDO_USER" git pull
    else
        git pull
    fi
else
    echo "✓ Клонирование репозитория в $REPO_DIR..."
    if [ -n "$SUDO_USER" ]; then
        sudo -u "$SUDO_USER" git clone "$REPO_URL" "$REPO_DIR"
    else
        git clone "$REPO_URL" "$REPO_DIR"
    fi
    cd "$REPO_DIR"
fi

# Сборка модулей
echo ""
echo "=== Сборка модулей ==="
make clean
make

# Установка модулей
echo ""
echo "=== Установка модулей ==="
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install

# Копирование udev правил
echo ""
echo "=== Копирование udev правил ==="
if [ -d files/usr ]; then
    cd files/usr
    find . -type f -exec install -D -m 644 {} /{} \;
    cd ../..
fi

# Создание конфигурации автозагрузки
echo ""
echo "=== Настройка автозагрузки ==="
tee /etc/modules-load.d/tuxedo-drivers.conf << 'EOF'
# Tuxedo drivers for Clevo notebooks
clevo_acpi
ite_829x
tuxedo_keyboard
tuxedo_io
tuxedo_compatibility_check
EOF

# Обновление зависимостей
echo ""
echo "=== Обновление зависимостей ==="
depmod -a
update-initramfs -u -k all

# Обновление hwdb
if command -v systemd-hwdb >/dev/null 2>&1; then
    echo "✓ Обновление hwdb..."
    systemd-hwdb update 2>/dev/null || true
fi

# Перезагрузка udev
echo "✓ Перезагрузка udev..."
udevadm control --reload-rules
udevadm trigger

# Проверка установки
echo ""
echo "=== Проверка установки ==="
MODULES_COUNT=$(find /lib/modules/$(uname -r)/updates/src -name "*.ko.xz" 2>/dev/null | wc -l)
echo "Установлено модулей: $MODULES_COUNT"

echo ""
echo "=== Конфигурация автозагрузки ==="
cat /etc/modules-load.d/tuxedo-drivers.conf

echo ""
echo "=== Установка завершена ==="
echo "Драйверы сохранены в: $REPO_DIR"
echo "Для применения изменений перезагрузитесь: reboot"