#!/bin/bash
set -e

#==============================================================================
# Clevo Fan Control - Installation Script
#==============================================================================

VERSION="1.0.0"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
COMPILED_DIR="$SCRIPT_DIR/compiled"
BINARY_NAME="clevo-fan-control"
BINARY_PATH="$BUILD_DIR/$BINARY_NAME"
COMPILED_BINARY_PATH="$COMPILED_DIR/$BINARY_NAME"
SERVICE_NAME="clevo-fan-control.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_NAME"
SYSTEM_CONFIG_DIR="/etc/clevo-fan-control"
SYSTEM_LOG_DIR="/var/log/clevo-fan-control"
SYSTEM_STATE_DIR="/var/lib/clevo-fan-control"

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Флаги
INSTALL_DRIVERS=0
SKIP_DRIVERS=0
REBUILD=0
PKG_MANAGER=""
PKG_UPDATE=""
PKG_INSTALL=""
PKG_DEPS=()

#==============================================================================
# Функции для вывода
#==============================================================================

print_header() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║     Clevo Fan Control - Installation Script v$VERSION              ║"
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

#==============================================================================
# Проверка прав
#==============================================================================

check_root() {
    if [ "$EUID" -ne 0 ]; then
        print_error "Этот скрипт должен запускаться с правами root"
        echo "Запустите: sudo $0"
        exit 1
    fi
}

#==============================================================================
# Проверка ОС и определение пакетного менеджера
#==============================================================================

check_os() {
    print_step "Проверка операционной системы"
    
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        print_info "ОС: $NAME $VERSION_ID"
    else
        print_warning "Не удалось определить ОС"
    fi
    
    # Определяем пакетный менеджер
    if command -v apt >/dev/null 2>&1; then
        PKG_MANAGER="apt"
        PKG_UPDATE="apt update -qq"
        PKG_INSTALL="apt install -y"
        PKG_DEPS=(
            build-essential
            cmake
            make
            g++
            libc6-dev
            linux-headers-$(uname -r)
            git
            pkg-config
            libpthread-stubs0-dev
            libsystemd-dev
        )
        print_success "Обнаружен APT (Debian, Ubuntu, Linux Mint, MX Linux)"
        
    elif command -v dnf >/dev/null 2>&1; then
        PKG_MANAGER="dnf"
        PKG_UPDATE="dnf check-update -q"
        PKG_INSTALL="dnf install -y"
        PKG_DEPS=(
            gcc-c++
            cmake
            make
            kernel-devel
            git
            pkgconfig
            systemd-devel
        )
        print_success "Обнаружен DNF (Fedora, RHEL, CentOS)"
        
    elif command -v yum >/dev/null 2>&1; then
        PKG_MANAGER="yum"
        PKG_UPDATE="yum check-update -q"
        PKG_INSTALL="yum install -y"
        PKG_DEPS=(
            gcc-c++
            cmake
            make
            kernel-devel
            git
            pkgconfig
            systemd-devel
        )
        print_success "Обнаружен YUM (RHEL, CentOS)"
        
    elif command -v pacman >/dev/null 2>&1; then
        PKG_MANAGER="pacman"
        PKG_UPDATE="pacman -Sy --noconfirm"
        PKG_INSTALL="pacman -S --noconfirm"
        PKG_DEPS=(
            base-devel
            cmake
            make
            gcc
            linux-headers
            git
            pkg-config
            systemd-libs
        )
        print_success "Обнаружен Pacman (Arch Linux, Manjaro)"
        
    elif command -v zypper >/dev/null 2>&1; then
        PKG_MANAGER="zypper"
        PKG_UPDATE="zypper refresh -q"
        PKG_INSTALL="zypper install -y"
        PKG_DEPS=(
            gcc-c++
            cmake
            make
            kernel-devel
            git
            pkg-config
            systemd-devel
        )
        print_success "Обнаружен Zypper (openSUSE)"
        
    else
        print_error "Не удалось определить пакетный менеджер"
        print_warning "Продолжение установки может быть невозможным"
        if ! ask_yes_no "Продолжить установку вручную?"; then
            exit 1
        fi
        PKG_MANAGER="unknown"
    fi
    
    echo ""
    print_info "Бинарный файл будет работать на любом дистрибутиве Linux"
    print_info "Пакетный менеджер используется только для установки зависимостей"
}

#==============================================================================
# Проверка наличия скомпилированного бинарного файла
#==============================================================================

check_existing_binary() {
    print_step "Проверка наличия скомпилированного бинарного файла"
    
    local found_binary=""
    
    if [ -f "$COMPILED_BINARY_PATH" ]; then
        found_binary="$COMPILED_BINARY_PATH"
        print_info "Найден скомпилированный бинарный файл в ./compiled/"
    elif [ -f "$BINARY_PATH" ]; then
        found_binary="$BINARY_PATH"
        print_info "Найден скомпилированный бинарный файл в ./build/"
    fi
    
    if [ -n "$found_binary" ]; then
        local binary_version=$($found_binary help 2>/dev/null | grep "Clevo Fan Control v" | head -1 | sed 's/.*v\([0-9.]*\).*/\1/')
        if [ -n "$binary_version" ]; then
            print_info "Версия найденного бинарного файла: v$binary_version"
        fi
        
        echo ""
        if ask_yes_no "Использовать существующий бинарный файл? (пересборка будет пропущена)"; then
            BINARY_PATH="$found_binary"
            REBUILD=1
            print_success "Будет использован существующий бинарный файл"
            return 0
        else
            print_info "Будет выполнена пересборка"
            REBUILD=0
            return 1
        fi
    else
        print_info "Скомпилированный бинарный файл не найден, будет выполнена сборка"
        REBUILD=0
        return 1
    fi
}

#==============================================================================
# Установка зависимостей
#==============================================================================

install_dependencies() {
    print_step "Установка зависимостей для сборки"
    
    if [ "$REBUILD" -eq 1 ]; then
        print_info "Пересборка не требуется, установка зависимостей пропущена"
        return 0
    fi
    
    if [ "$PKG_MANAGER" = "unknown" ]; then
        print_warning "Пропуск автоматической установки зависимостей"
        print_info "Убедитесь, что установлены: g++, cmake, make, linux-headers, git, pkg-config, libsystemd-dev"
        return 0
    fi
    
    print_info "Обновление списка пакетов..."
    eval "$PKG_UPDATE" 2>/dev/null || true
    
    print_info "Установка пакетов: ${PKG_DEPS[*]}"
    eval "$PKG_INSTALL ${PKG_DEPS[*]}"
    
    # Особые случаи для некоторых дистрибутивов
    if [ "$PKG_MANAGER" = "pacman" ]; then
        local kernel_version=$(uname -r | cut -d'-' -f1)
        pacman -S --noconfirm linux${kernel_version:0:1}-headers 2>/dev/null || true
    fi
    
    print_success "Зависимости установлены"
}

#==============================================================================
# Установка драйверов Tuxedo (опционально)
#==============================================================================

check_tuxedo_drivers() {
    print_step "Проверка драйверов Tuxedo"
    
    if lsmod | grep -q "clevo_acpi\|tuxedo_io" 2>/dev/null; then
        print_success "Драйверы Tuxedo уже установлены и загружены"
        print_info "Загруженные модули:"
        lsmod | grep -E "clevo|tuxedo" | while read line; do
            echo "  $line"
        done
        return 0
    fi
    
    if find /lib/modules/$(uname -r) -name "*clevo*.ko*" 2>/dev/null | grep -q .; then
        print_warning "Драйверы Tuxedo установлены, но не загружены"
        print_info "Попытка загрузить модули..."
        modprobe clevo_acpi 2>/dev/null && print_success "Модуль clevo_acpi загружен"
        modprobe tuxedo_io 2>/dev/null && print_success "Модуль tuxedo_io загружен"
        modprobe tuxedo_keyboard 2>/dev/null && print_success "Модуль tuxedo_keyboard загружен"
        
        if lsmod | grep -q "clevo_acpi\|tuxedo_io"; then
            return 0
        fi
    fi
    
    return 1
}

install_tuxedo_drivers() {
    print_step "Установка драйверов Tuxedo"
    
    local driver_script="$SCRIPT_DIR/update-tuxedo-drivers.sh"
    
    if [ -f "$driver_script" ]; then
        print_info "Запуск скрипта установки драйверов..."
        chmod +x "$driver_script"
        bash "$driver_script"
        print_success "Драйверы Tuxedo установлены"
    else
        print_warning "Скрипт update-tuxedo-drivers.sh не найден"
        print_info "Установка драйверов из репозитория..."
        
        local repo_dir="/tmp/tuxedo-drivers"
        
        git clone https://github.com/tuxedocomputers/tuxedo-drivers.git "$repo_dir"
        
        cd "$repo_dir"
        make clean
        make
        make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install
        depmod -a
        
        if [ -d files/usr ]; then
            cd files/usr
            find . -type f -exec install -D -m 644 {} /{} \;
            cd ../..
        fi
        
        cat > /etc/modules-load.d/tuxedo-drivers.conf << 'EOF'
# Tuxedo drivers for Clevo notebooks
clevo_acpi
tuxedo_keyboard
tuxedo_io
EOF
        
        cd "$SCRIPT_DIR"
        rm -rf "$repo_dir"
        
        print_success "Драйверы Tuxedo установлены"
    fi
    
    print_info "Загрузка модулей..."
    modprobe clevo_acpi 2>/dev/null && print_success "Модуль clevo_acpi загружен"
    modprobe tuxedo_io 2>/dev/null && print_success "Модуль tuxedo_io загружен"
    modprobe tuxedo_keyboard 2>/dev/null && print_success "Модуль tuxedo_keyboard загружен"
    
    update-initramfs -u -k all 2>/dev/null || true
    
    print_success "Установка драйверов завершена"
    print_warning "Для полного применения драйверов рекомендуется перезагрузить систему"
}

#==============================================================================
# Сборка приложения
#==============================================================================

build_application() {
    if [ "$REBUILD" -eq 1 ]; then
        print_step "Использование существующего бинарного файла"
        print_info "Бинарный файл: $BINARY_PATH"
        return 0
    fi
    
    print_step "Сборка Clevo Fan Control"
    
    if [ -d "$BUILD_DIR" ]; then
        print_info "Очистка директории сборки..."
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    print_info "Конфигурация CMake..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR"
    
    print_info "Компиляция..."
    make -j$(nproc)
    
    print_info "Установка прав доступа..."
    chown root "$BINARY_PATH"
    chmod u+s "$BINARY_PATH"
    
    cd "$SCRIPT_DIR"
    print_success "Сборка завершена"
}

#==============================================================================
# Установка в систему
#==============================================================================

setup_system_directories() {
    print_step "Создание системных директорий"
    
    mkdir -p "$SYSTEM_CONFIG_DIR"
    mkdir -p "$SYSTEM_LOG_DIR"
    mkdir -p "$SYSTEM_STATE_DIR"
    
    chmod 755 "$SYSTEM_CONFIG_DIR"
    chmod 755 "$SYSTEM_LOG_DIR"
    chmod 755 "$SYSTEM_STATE_DIR"
    
    print_success "Системные директории созданы:"
    echo "  • Конфигурация: $SYSTEM_CONFIG_DIR"
    echo "  • Логи:         $SYSTEM_LOG_DIR"
    echo "  • Состояние:    $SYSTEM_STATE_DIR"
    echo ""
    print_info "Конфигурационный файл будет создан автоматически при первом запуске"
}

install_binary() {
    print_step "Установка бинарного файла"
    
    local install_path="/usr/local/bin/$BINARY_NAME"
    cp "$BINARY_PATH" "$install_path"
    chmod 755 "$install_path"
    
    chown root "$install_path"
    chmod u+s "$install_path"
    
    print_success "Бинарный файл установлен в $install_path"
    print_info "Установлен setuid root (для доступа к EC портам)"
}

#==============================================================================
# Установка systemd сервиса
#==============================================================================

install_service() {
    print_step "Установка systemd сервиса"
    
    cat > "$SERVICE_PATH" << 'EOF'
[Unit]
Description=Clevo Fan Control Daemon
After=multi-user.target
Wants=network.target

[Service]
Type=simple
User=root
Group=root
Environment=HOME=/root
Environment=XDG_CONFIG_HOME=/etc
Environment=XDG_DATA_HOME=/var/lib
Environment=XDG_STATE_HOME=/var/lib
ExecStart=/usr/local/bin/clevo-fan-control
ExecStop=/usr/local/bin/clevo-fan-control stop
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
    
    chmod 644 "$SERVICE_PATH"
    print_success "Файл сервиса создан в $SERVICE_PATH"
    
    systemctl daemon-reload
    print_success "systemd перезагружен"
    
    systemctl enable "$SERVICE_NAME"
    print_success "Сервис добавлен в автозапуск"
}

#==============================================================================
# Создание пользовательских ссылок
#==============================================================================

create_user_links() {
    print_step "Создание пользовательских ссылок"
    
    if [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != "root" ]; then
        local USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
        
        if [ -n "$USER_HOME" ]; then
            local USER_CONFIG_DIR="$USER_HOME/.config/clevo-fan-control"
            local USER_LOG_DIR="$USER_HOME/.local/share/clevo-fan-control/logs"
            local USER_STATE_DIR="$USER_HOME/.local/state/clevo-fan-control"
            
            mkdir -p "$(dirname "$USER_CONFIG_DIR")"
            mkdir -p "$(dirname "$USER_LOG_DIR")"
            mkdir -p "$(dirname "$USER_STATE_DIR")"
            
            ln -sf "$SYSTEM_CONFIG_DIR" "$USER_CONFIG_DIR" 2>/dev/null || true
            ln -sf "$SYSTEM_LOG_DIR" "$USER_LOG_DIR" 2>/dev/null || true
            ln -sf "$SYSTEM_STATE_DIR" "$USER_STATE_DIR" 2>/dev/null || true
            
            chown -h "$SUDO_USER":"$SUDO_USER" "$USER_CONFIG_DIR" 2>/dev/null || true
            chown -h "$SUDO_USER":"$SUDO_USER" "$USER_LOG_DIR" 2>/dev/null || true
            chown -h "$SUDO_USER":"$SUDO_USER" "$USER_STATE_DIR" 2>/dev/null || true
            
            print_success "Созданы пользовательские ссылки для $SUDO_USER"
        fi
    fi
}

#==============================================================================
# Запуск сервиса
#==============================================================================

start_service() {
    print_step "Запуск сервиса"
    
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        print_info "Остановка старого экземпляра..."
        systemctl stop "$SERVICE_NAME"
    fi
    
    systemctl start "$SERVICE_NAME"
    sleep 2
    
    if systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Сервис успешно запущен"
    else
        print_error "Не удалось запустить сервис"
        systemctl status "$SERVICE_NAME"
        exit 1
    fi
}

#==============================================================================
# Вывод информации
#==============================================================================

print_summary() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════╗"
    echo "║                    УСТАНОВКА ЗАВЕРШЕНА                          ║"
    echo "╚══════════════════════════════════════════════════════════════════╝"
    echo ""
    print_success "Clevo Fan Control v$VERSION успешно установлен"
    echo ""
    echo "Информация об установке:"
    echo "  • Бинарный файл:    /usr/local/bin/clevo-fan-control"
    echo "  • Конфигурация:     /etc/clevo-fan-control/"
    echo "  • Логи:             /var/log/clevo-fan-control/"
    echo "  • Сервис:           $SERVICE_NAME"
    echo "  • Скрипт удаления:  /usr/local/bin/clevo-fan-control-uninstall"
    echo ""
    echo "Управление:"
    echo "  • Запуск:           sudo systemctl start clevo-fan-control"
    echo "  • Остановка:        sudo systemctl stop clevo-fan-control"
    echo "  • Статус:           sudo systemctl status clevo-fan-control"
    echo "  • Логи:             sudo journalctl -u clevo-fan-control -f"
    echo ""
    echo "Пользовательские команды:"
    echo "  • Статус:           sudo clevo-fan-control status"
    echo "  • Остановка:        sudo clevo-fan-control stop"
    echo "  • Тест:             sudo clevo-fan-control test"
    echo "  • Логирование:      sudo clevo-fan-control log"
    echo "  • Справка:          sudo clevo-fan-control help"
    echo ""
    
    if [ "$INSTALL_DRIVERS" -eq 1 ]; then
        echo -e "${YELLOW}⚠ ВНИМАНИЕ: Были установлены драйверы Tuxedo${NC}"
        echo -e "${YELLOW}  Рекомендуется перезагрузить систему для полного применения драйверов${NC}"
        echo ""
    fi
}

#==============================================================================
# Главная функция
#==============================================================================

main() {
    print_header
    
    check_root
    check_os
    check_existing_binary
    install_dependencies
    
    print_step "Проверка драйверов Tuxedo"
    
    if check_tuxedo_drivers; then
        print_success "Драйверы Tuxedo уже установлены и работают"
        SKIP_DRIVERS=1
    else
        echo ""
        print_warning "Драйверы Tuxedo не обнаружены"
        echo ""
        echo "Драйверы Tuxedo обеспечивают:"
        echo "  • Корректную работу подсветки клавиатуры"
        echo "  • Дополнительные датчики температуры"
        echo "  • Полную совместимость с аппаратным обеспечением"
        echo ""
        
        if ask_yes_no "Установить драйверы Tuxedo?"; then
            INSTALL_DRIVERS=1
        else
            print_info "Установка драйверов пропущена"
            print_warning "Некоторые функции могут работать некорректно"
            SKIP_DRIVERS=1
        fi
    fi
    
    if [ "$INSTALL_DRIVERS" -eq 1 ]; then
        install_tuxedo_drivers
    fi
    
    build_application
    setup_system_directories
    install_binary
    install_service
    create_user_links
    start_service
    
    print_summary
}

#==============================================================================
# Запуск
#==============================================================================

main "$@"