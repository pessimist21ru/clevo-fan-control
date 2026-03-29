# Установка драйверов Tuxedo для ноутбуков Clevo в Debian 13

## 📋 Оглавление
1. [Подготовка системы](#подготовка-системы)
2. [Автоматическая установка (рекомендуется)](#автоматическая-установка-рекомендуется)
3. [Ручная установка](#ручная-установка)
4. [Настройка автозагрузки](#настройка-автозагрузки)
5. [Проверка работы](#проверка-работы)
6. [Устранение проблем](#устранение-проблем)

---

## Подготовка системы

### Установка необходимых пакетов

```bash
# Обновление списка пакетов
sudo apt update

# Установка зависимостей для сборки
sudo apt install -y \
    build-essential \
    dkms \
    git \
    make \
    linux-headers-$(uname -r) \
    debhelper \
    devscripts \
    python3 \
    python3-pip

#Проверка версии ядра
uname -r
# Должно быть: 6.12.74+deb13+1-amd64 или выше
```

### Автоматическая установка (рекомендуется)

#### Скрипт установки

Создайте файл `~/update-tuxedo-drivers.sh` с помощью `tee`:

```bash
sudo tee ~/update-tuxedo-drivers.sh << 'SCRIPT'
#!/bin/bash
set -e

# Проверка прав root
if [ "$EUID" -ne 0 ]; then 
    echo "Пожалуйста, запустите с sudo: sudo $0"
    exit 1
fi

# Определение домашней директории пользователя
if [ -n "$SUDO_USER" ]; then
    USER_HOME=$(getent passwd $SUDO_USER | cut -d: -f6)
else
    USER_HOME=$HOME
fi

REPO_DIR="$USER_HOME/install/linux/drivers/clevo/tuxedo-drivers"
REPO_URL="https://github.com/tuxedocomputers/tuxedo-drivers.git"

echo "=== Tuxedo Drivers Installer/Updater ==="
echo ""

# Клонирование или обновление репозитория
cd "$USER_HOME"
if [ -d "$REPO_DIR" ]; then
    echo "✓ Обновление репозитория..."
    cd "$REPO_DIR"
    sudo -u "$SUDO_USER" git pull
else
    echo "✓ Клонирование репозитория..."
    mkdir -p "$(dirname "$REPO_DIR")"
    sudo -u "$SUDO_USER" git clone "$REPO_URL" "$REPO_DIR"
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
echo "Для применения изменений перезагрузитесь: reboot"
SCRIPT

chmod +x ~/install-tuxedo.sh
```

#### Запуск установки

```bash
sudo ~/install-tuxedo.sh
```

#### После установки

```bash
sudo reboot
```

### Ручная установка

Если автоматический скрипт не сработал, выполните шаги вручную.
1. Клонирование репозитория

```bash
cd ~
git clone https://github.com/tuxedocomputers/tuxedo-drivers.git
cd tuxedo-drivers
```

2. Установка зависимостей

```bash
sudo apt install -y build-essential dkms git make linux-headers-$(uname -r)
```

3. Сборка модулей

```bash
make clean
make
```

4. Установка модулей

```bash
# Установка через kbuild
sudo make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install
```

5. Копирование вспомогательных файлов

```bash
# Копирование udev правил и hwdb
if [ -d files/usr ]; then
    cd files/usr
    sudo find . -type f -exec install -D -m 644 {} /{} \;
    cd ../..
fi
```

6. Обновление зависимостей

```bash
sudo depmod -a
sudo update-initramfs -u -k all
sudo systemd-hwdb update 2>/dev/null || true
sudo udevadm control --reload-rules
sudo udevadm trigger
```

7. Загрузка модулей

```bash
# Выгрузка старых модулей
sudo modprobe -r tuxedo_keyboard tuxedo_io clevo_acpi 2>/dev/null

# Загрузка новых
sudo modprobe clevo_acpi
sudo modprobe tuxedo_keyboard
sudo modprobe tuxedo_io
```

8. Перезагрузка

```bash
sudo reboot
```


### Настройка автозагрузки

#### Создание конфигурации модулей

```bash
sudo tee /etc/modules-load.d/tuxedo-drivers.conf << 'EOF'
# Tuxedo drivers for Clevo notebooks
clevo_acpi
tuxedo_keyboard
tuxedo_io
EOF
```

#### Проверка автозагрузки

```bash
cat /etc/modules-load.d/tuxedo-drivers.conf
```

## Проверка работы

### Проверка загруженных модулей

```bash
lsmod | grep -E "tuxedo|clevo"
```

Ожидаемый вывод:

```bash
clevo_acpi             20480  0
tuxedo_io              24576  0
tuxedo_keyboard       122880  2 clevo_acpi,tuxedo_io
tuxedo_compatibility_check    12288  1 tuxedo_keyboard
```

### Проверка подсветки клавиатуры

```bash
# Список интерфейсов подсветки
ls /sys/class/leds/ | grep kbd

# Текущая яркость
cat /sys/class/leds/*kbd_backlight/brightness

# Установка яркости (например, 50%)
echo 50 | sudo tee /sys/class/leds/*kbd_backlight/brightness
```

### Проверка udev правил

```bash
# Проверка установленных правил
ls -la /etc/udev/rules.d/99-tuxedo-*.rules

# Проверка hwdb
ls -la /etc/udev/hwdb.d/61-*-tuxedo.hwdb
```

### Проверка версии модулей

```bash
modinfo clevo_acpi | grep -E "filename|version"
modinfo tuxedo_keyboard | grep -E "filename|version"
```


## Устранение проблем

### Проблема: Ошибка при сборке "generated/autoconf.h: No such file"

**Решение**: Переустановка заголовков ядра

```bash
sudo apt install --reinstall linux-headers-$(uname -r)
```


### Проблема: SSL ошибки при подписи модулей

**Решение**: Ошибки не критичны, модули работают. Для отключения подписи:

```bash
# Создание ключа подписи (опционально)
sudo /usr/src/linux-headers-$(uname -r)/scripts/sign-file \
    sha256 /var/lib/dkms/mok.key /var/lib/dkms/mok.pub \
    /lib/modules/$(uname -r)/updates/src/*.ko
```

### Проблема: Модули не загружаются после перезагрузки

**Решение**: Проверка автозагрузки

```bash
# Проверка конфигурации
cat /etc/modules-load.d/tuxedo-drivers.conf

# Ручная загрузка
sudo modprobe clevo_acpi
sudo modprobe tuxedo_keyboard

# Добавление в автозагрузку
sudo tee -a /etc/modules-load.d/tuxedo-drivers.conf << 'EOF'
clevo_acpi
tuxedo_keyboard
EOF
```

### Проблема: Конфликт с управлением вентиляторами

Если у вас есть собственная утилита управления вентиляторами, которая конфликтует с модулями Tuxedo:

```bash
# Выгрузка проблемных модулей
sudo modprobe -r tuxedo_io clevo_acpi

# Оставить только подсветку
sudo modprobe tuxedo_keyboard

# Добавить в черный список
sudo tee /etc/modprobe.d/tuxedo-blacklist.conf << 'EOF'
blacklist tuxedo_io
blacklist clevo_acpi
EOF
```

### Проблема: Нет подсветки клавиатуры

**Решение**: Проверка Fn-клавиш в настройках окружения рабочего стола

```bash
# Проверка, что модуль загружен
lsmod | grep tuxedo_keyboard

# Ручная установка яркости
echo 100 | sudo tee /sys/class/leds/*kbd_backlight/brightness
```

## Дополнительная информация

### Структура установки

Компонент	Расположение
Модули ядра	/lib/modules/$(uname -r)/updates/src/
udev правила	/etc/udev/rules.d/99-tuxedo-*.rules
hwdb	/etc/udev/hwdb.d/61-*-tuxedo.hwdb
Черные списки	/etc/modprobe.d/tuxedo-*.conf
Автозагрузка	/etc/modules-load.d/tuxedo-drivers.conf

### Полезные команды

```bash
# Просмотр логов модулей
sudo dmesg | grep -i tuxedo
sudo journalctl -k | grep -i clevo

# Информация о модуле
modinfo tuxedo_keyboard

# Перезагрузка udev после изменения правил
sudo udevadm control --reload-rules
sudo udevadm trigger

# Обновление initramfs
sudo update-initramfs -u -k all
```

### Ссылки

    [Официальный репозиторий Tuxedo Drivers](https://github.com/tuxedocomputers/tuxedo-drivers)

    [GitLab (для багрепортов)] (https://gitlab.com/tuxedocomputers/development/packages/tuxedo-drivers)

    [Документация Tuxedo] (https://www.tuxedocomputers.com/)


    
#### Документация актуальна для Debian 13 (trixie) и ядра 6.12.74
Последнее обновление: 28 марта 2026
EOF

echo "=== Документация обновлена ==="
echo "Файл: ~/install/linux/drivers/clevo/Install_tuxedo_drivers.md"
echo ""
echo "Основные изменения:"
echo " • cat > file << 'EOF' заменён на sudo tee file << 'EOF'"
echo " • Добавлены закрывающие ``` после всех блоков кода"
echo " • Исправлена опечатка в linux-headers-$(uname -r)"
