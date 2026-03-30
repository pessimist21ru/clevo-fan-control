#!/bin/bash
# Clevo Fan Control - Uninstall Script

set -e

#==============================================================================
# Константы
#==============================================================================

SERVICE_NAME="clevo-fan-control.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_NAME"
SYSTEM_CONFIG_DIR="/etc/clevo-fan-control"
SYSTEM_LOG_DIR="/var/log/clevo-fan-control"
SYSTEM_STATE_DIR="/var/lib/clevo-fan-control"
BINARY_PATH="/usr/local/bin/clevo-fan-control"
UNINSTALL_SCRIPT="/usr/local/bin/clevo-fan-control-uninstall"

# Цвета
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

#==============================================================================
# Функции
#==============================================================================

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

echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║              Clevo Fan Control - Uninstall Script                ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""

print_step() {
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${BLUE}▶ $1${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# Остановка и отключение сервиса
print_step "Остановка и отключение сервиса"

if systemctl is-active --quiet "$SERVICE_NAME"; then
    systemctl stop "$SERVICE_NAME"
    print_success "Сервис остановлен"
fi

if systemctl is-enabled --quiet "$SERVICE_NAME" 2>/dev/null; then
    systemctl disable "$SERVICE_NAME"
    print_success "Сервис отключен"
fi

# Удаление файлов
print_step "Удаление файлов"

if [ -f "$BINARY_PATH" ]; then
    rm -f "$BINARY_PATH"
    print_success "Бинарный файл удалён"
fi

if [ -f "$SERVICE_PATH" ]; then
    rm -f "$SERVICE_PATH"
    print_success "Файл сервиса удалён"
fi

if [ -f "$UNINSTALL_SCRIPT" ] && [ "$UNINSTALL_SCRIPT" != "$0" ]; then
    rm -f "$UNINSTALL_SCRIPT"
    print_success "Скрипт удаления удалён"
fi

# Удаление конфигурации
print_step "Удаление конфигурационных файлов"

if ask_yes_no "Удалить конфигурационные файлы? (рекомендуется)"; then
    if [ -d "$SYSTEM_CONFIG_DIR" ]; then
        rm -rf "$SYSTEM_CONFIG_DIR"
        print_success "Конфигурация удалена: $SYSTEM_CONFIG_DIR"
    fi
    
    if [ -d "$SYSTEM_LOG_DIR" ]; then
        rm -rf "$SYSTEM_LOG_DIR"
        print_success "Логи удалены: $SYSTEM_LOG_DIR"
    fi
    
    if [ -d "$SYSTEM_STATE_DIR" ]; then
        rm -rf "$SYSTEM_STATE_DIR"
        print_success "Состояние удалено: $SYSTEM_STATE_DIR"
    fi
else
    print_info "Конфигурационные файлы сохранены"
fi

# Удаление пользовательских ссылок
print_step "Удаление пользовательских ссылок"

if [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != "root" ]; then
    USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    
    if [ -n "$USER_HOME" ]; then
        USER_CONFIG_LINK="$USER_HOME/.config/clevo-fan-control"
        USER_LOG_DIR="$USER_HOME/.local/share/clevo-fan-control"
        USER_STATE_DIR="$USER_HOME/.local/state/clevo-fan-control"
        
        if [ -L "$USER_CONFIG_LINK" ]; then
            rm -f "$USER_CONFIG_LINK"
            print_success "Удалена ссылка: $USER_CONFIG_LINK"
        fi
        
        if [ -d "$USER_LOG_DIR" ]; then
            rm -rf "$USER_LOG_DIR"
            print_success "Удалена директория: $USER_LOG_DIR"
        fi
        
        if [ -d "$USER_STATE_DIR" ]; then
            rm -rf "$USER_STATE_DIR"
            print_success "Удалена директория: $USER_STATE_DIR"
        fi
    fi
fi

# Удаление драйверов Tuxedo (опционально)
print_step "Удаление драйверов Tuxedo"

if ask_yes_no "Удалить драйверы Tuxedo? (могут использоваться другими программами)"; then
    print_info "Выгрузка модулей..."
    modprobe -r tuxedo_keyboard tuxedo_io clevo_acpi 2>/dev/null && print_success "Модули выгружены" || print_warning "Не удалось выгрузить модули"
    
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
    print_warning "Рекомендуется перезагрузить систему"
else
    print_info "Драйверы Tuxedo сохранены"
fi

# Перезагрузка systemd
print_step "Финальные операции"

systemctl daemon-reload
print_success "systemd перезагружен"

# Завершение
echo ""
echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║                    УДАЛЕНИЕ ЗАВЕРШЕНО                           ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo ""
print_success "Clevo Fan Control удалён из системы"
echo ""