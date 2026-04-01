#!/bin/bash
# Clevo Fan Control - Uninstall Script
# Version: 1.0.2

set -e

#==============================================================================
# Константы
#==============================================================================

VERSION="1.0.2"
SERVICE_NAME="clevo-fan-control.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_NAME"
SYSTEM_CONFIG_DIR="/etc/clevo-fan-control"
SYSTEM_LOG_DIR="/var/log/clevo-fan-control"
SYSTEM_STATE_DIR="/var/lib/clevo-fan-control"
BINARY_PATH="/usr/local/bin/clevo-fan-control"

# Пути в домашней директории (пользовательский режим)
USER_CONFIG_DIR="$HOME/.config/clevo-fan-control"
USER_LOG_DIR="$HOME/.local/share/clevo-fan-control"
USER_STATE_DIR="$HOME/.local/state/clevo-fan-control"

# Пути в /root (на случай ошибочной установки как службы с XDG путями)
ROOT_CONFIG_DIR="/root/.config/clevo-fan-control"
ROOT_LOG_DIR="/root/.local/share/clevo-fan-control"
ROOT_STATE_DIR="/root/.local/state/clevo-fan-control"

# Цвета
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Флаги
CLEAN_ROOT=0

#==============================================================================
# Функции
#==============================================================================

print_header() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║         Clevo Fan Control - Uninstall Script v$VERSION              ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo ""
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

print_step() {
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${CYAN}▶ $1${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

ask_yes_no() {
    local prompt="$1"
    local answer
    while true; do
        read -p "$prompt (y/n): " -n 1 -r answer
        echo
        case $answer in
            [Yy]* ) return 0;;
            [Nn]* ) return 1;;
            * ) echo "Пожалуйста, ответьте y или n";;
        esac
    done
}

check_and_remove() {
    local path="$1"
    local description="$2"
    
    if [ -L "$path" ]; then
        rm -f "$path"
        print_success "Удалена ссылка: $path ($description)"
        return 0
    elif [ -d "$path" ]; then
        rm -rf "$path"
        print_success "Удалена директория: $path ($description)"
        return 0
    elif [ -f "$path" ]; then
        rm -f "$path"
        print_success "Удалён файл: $path ($description)"
        return 0
    fi
    return 1
}

#==============================================================================
# Проверка прав
#==============================================================================

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Запустите с sudo: sudo $0${NC}"
    exit 1
fi

#==============================================================================
# Удаление
#==============================================================================

print_header

# Остановка и отключение сервиса
print_step "Остановка и отключение сервиса"

if systemctl is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
    systemctl stop "$SERVICE_NAME"
    print_success "Сервис остановлен"
fi

if systemctl is-enabled --quiet "$SERVICE_NAME" 2>/dev/null; then
    systemctl disable "$SERVICE_NAME"
    print_success "Сервис отключен"
fi

# Удаление файлов сервиса и бинарника
print_step "Удаление системных файлов"

check_and_remove "$BINARY_PATH" "бинарный файл"
check_and_remove "$SERVICE_PATH" "файл сервиса"

# Удаление системных директорий (FHS)
print_step "Удаление системных директорий (FHS)"

if ask_yes_no "Удалить системные конфигурационные файлы? (рекомендуется)"; then
    check_and_remove "$SYSTEM_CONFIG_DIR" "системная конфигурация"
    check_and_remove "$SYSTEM_LOG_DIR" "системные логи"
    check_and_remove "$SYSTEM_STATE_DIR" "системное состояние"
else
    print_info "Системные файлы сохранены"
fi

# Удаление пользовательских директорий (XDG)
print_step "Удаление пользовательских директорий (XDG)"

if [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != "root" ]; then
    USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    
    if [ -n "$USER_HOME" ]; then
        USER_CONFIG_DIR="$USER_HOME/.config/clevo-fan-control"
        USER_LOG_DIR="$USER_HOME/.local/share/clevo-fan-control"
        USER_STATE_DIR="$USER_HOME/.local/state/clevo-fan-control"
        
        print_info "Проверка директорий пользователя $SUDO_USER..."
        
        check_and_remove "$USER_CONFIG_DIR" "пользовательская конфигурация"
        check_and_remove "$USER_LOG_DIR" "пользовательские логи"
        check_and_remove "$USER_STATE_DIR" "пользовательское состояние"
    fi
else
    # Если скрипт запущен напрямую от root (без sudo), проверяем домашнюю директорию root
    print_info "Проверка директорий пользователя root..."
    
    check_and_remove "$ROOT_CONFIG_DIR" "конфигурация root (ошибочная)"
    check_and_remove "$ROOT_LOG_DIR" "логи root (ошибочные)"
    check_and_remove "$ROOT_STATE_DIR" "состояние root (ошибочное)"
fi

# Дополнительная проверка для старых версий
print_step "Проверка остаточных файлов от старых версий"

OLD_PATHS=(
    "/usr/local/bin/clevo-fan-control-kde"
    "/usr/local/bin/clevo-fan-control-daemon"
    "/etc/clevo-fan-control-kde"
    "/var/log/clevo-fan-control-kde"
    "/var/lib/clevo-fan-control-kde"
    "$HOME/.config/clevo-fan-control-kde"
    "$HOME/.local/share/clevo-fan-control-kde"
)

for old_path in "${OLD_PATHS[@]}"; do
    check_and_remove "$old_path" "старая версия"
done

# Очистка /root (на случай ошибочной установки как службы с XDG путями)
print_step "Очистка /root (на случай ошибочной установки)"

if [ -d "/root/.config/clevo-fan-control" ] || \
   [ -d "/root/.local/share/clevo-fan-control" ] || \
   [ -d "/root/.local/state/clevo-fan-control" ]; then
    
    print_warning "Обнаружены остаточные файлы в /root от предыдущих версий"
    
    if ask_yes_no "Очистить /root от файлов clevo-fan-control?"; then
        check_and_remove "/root/.config/clevo-fan-control" "конфигурация root (ошибочная)"
        check_and_remove "/root/.local/share/clevo-fan-control" "логи root (ошибочные)"
        check_and_remove "/root/.local/state/clevo-fan-control" "состояние root (ошибочное)"
        CLEAN_ROOT=1
    else
        print_info "Очистка /root пропущена"
    fi
else
    print_info "Остаточных файлов в /root не обнаружено"
fi

# Удаление драйверов Tuxedo (опционально)
print_step "Удаление драйверов Tuxedo"

if lsmod | grep -q "clevo_acpi\|tuxedo_io\|tuxedo_keyboard" 2>/dev/null; then
    print_info "Обнаружены загруженные драйверы Tuxedo"
    
    if ask_yes_no "Удалить драйверы Tuxedo? (могут использоваться другими программами)"; then
        print_info "Выгрузка модулей..."
        modprobe -r tuxedo_keyboard tuxedo_io clevo_acpi 2>/dev/null && \
            print_success "Модули выгружены" || \
            print_warning "Не удалось выгрузить модули (возможно, используются)"
        
        print_info "Удаление файлов модулей..."
        find /lib/modules -name "*clevo*.ko*" -o -name "*tuxedo*.ko*" 2>/dev/null | while read module; do
            rm -f "$module"
            print_success "Удалён: $(basename $module)"
        done
        
        rm -f /etc/modules-load.d/tuxedo-drivers.conf 2>/dev/null
        rm -f /etc/udev/rules.d/99-tuxedo-*.rules 2>/dev/null
        rm -f /etc/udev/hwdb.d/61-*-tuxedo.hwdb 2>/dev/null
        
        depmod -a
        print_success "Драйверы Tuxedo удалены"
        print_warning "Рекомендуется перезагрузить систему для полного удаления модулей из памяти"
    else
        print_info "Драйверы Tuxedo сохранены"
    fi
else
    print_info "Драйверы Tuxedo не загружены"
fi

# Перезагрузка systemd
print_step "Финальные операции"

systemctl daemon-reload
print_success "systemd перезагружен"

# Проверка успешности удаления
print_step "Проверка удаления"

REMAINING_FILES=0

check_remaining() {
    local path="$1"
    local name="$2"
    if [ -e "$path" ]; then
        print_warning "Остался файл: $path ($name)"
        REMAINING_FILES=1
    fi
}

check_remaining "$BINARY_PATH" "бинарный файл"
check_remaining "$SERVICE_PATH" "файл сервиса"
check_remaining "$SYSTEM_CONFIG_DIR" "системная конфигурация"
check_remaining "$SYSTEM_LOG_DIR" "системные логи"
check_remaining "$SYSTEM_STATE_DIR" "системное состояние"

if [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != "root" ]; then
    USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    if [ -n "$USER_HOME" ]; then
        check_remaining "$USER_HOME/.config/clevo-fan-control" "пользовательская конфигурация"
        check_remaining "$USER_HOME/.local/share/clevo-fan-control" "пользовательские логи"
        check_remaining "$USER_HOME/.local/state/clevo-fan-control" "пользовательское состояние"
    fi
fi

if [ "$CLEAN_ROOT" -eq 1 ]; then
    check_remaining "/root/.config/clevo-fan-control" "конфигурация root"
    check_remaining "/root/.local/share/clevo-fan-control" "логи root"
    check_remaining "/root/.local/state/clevo-fan-control" "состояние root"
fi

if [ $REMAINING_FILES -eq 0 ]; then
    print_success "Все файлы удалены"
else
    print_warning "Некоторые файлы не были удалены (возможно, созданы вручную)"
fi

# Завершение
echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║                    УДАЛЕНИЕ ЗАВЕРШЕНО                           ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
print_success "Clevo Fan Control v$VERSION удалён из системы"

if [ "$INSTALL_DRIVERS" -eq 1 ] 2>/dev/null; then
    echo ""
    print_warning "⚠ ВНИМАНИЕ: Были удалены драйверы Tuxedo"
    print_warning "  Рекомендуется перезагрузить систему"
fi

echo ""