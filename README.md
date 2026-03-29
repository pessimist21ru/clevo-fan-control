# Clevo Fan Control for KDE Plasma

Утилита для управления вентиляторами на ноутбуках Clevo с интеграцией в системный трей KDE Plasma.

## Возможности

- Отображение температуры CPU/GPU
- Отображение скорости вентилятора (RPM и %)
- Автоматическое управление вентилятором
- Ручное управление (0-100%)
- Интеграция с KDE Plasma системным треем
- Динамические иконки в зависимости от нагрузки

## Требования

- KDE Plasma 5
- Qt5 (Core, Widgets, Gui)
- KF5::StatusNotifierItem

## Установка зависимостей

### Ubuntu/Debian:
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    qt6-base-dev \
    qt6-base-dev-tools \
    libkf6notifications-dev \
    libkf6statusnotifieritem-dev \
    libkf6widgetsaddons-dev \
    libkf6i18n-dev \
    libkf6coreaddons-dev \
    libkf6config-dev \
    libkf6windowsystem-dev \
    libkf6dbusaddons-dev \
    pkg-config
```

### Fedora/RHEL:
```bash
sudo dnf install -y \
    build-essential \
    cmake \
    qt6-base-dev \
    qt6-base-dev-tools \
    libkf6notifications-dev \
    libkf6statusnotifieritem-dev \
    libkf6widgetsaddons-dev \
    libkf6i18n-dev \
    libkf6coreaddons-dev \
    libkf6config-dev \
    libkf6windowsystem-dev \
    libkf6dbusaddons-dev \
    pkg-config
```

### Arch Linux:
```bash
sudo pacman -S --noconfirm \
    build-essential \
    cmake \
    qt6-base-dev \
    qt6-base-dev-tools \
    libkf6notifications-dev \
    libkf6statusnotifieritem-dev \
    libkf6widgetsaddons-dev \
    libkf6i18n-dev \
    libkf6coreaddons-dev \
    libkf6config-dev \
    libkf6windowsystem-dev \
    libkf6dbusaddons-dev \
    pkg-config
```

## Сборка и установка
### Клонирование репозитория
```bash
git clone https://github.com/yourusername/clevo-indicator-kde.git
cd clevo-indicator-kde
```

### Создание директории для сборки
```bash
mkdir build && cd build
```

### Конфигурация (автоматически проверит и установит зависимости)
```bash
cmake ..
```

### Сборка
```bash
make
```

### Установка (запросит sudo для setuid root)
```bash
sudo make install
```

### Установка с помощью скрипта

#### 1. Делаем скрипт установки исполняемым
chmod +x install.sh

#### 2. Запускаем установку
./install.sh

#### 3. Запускаем программу
clevo-indicator

## Использование

# Запуск с графическим интерфейсом
```bash
clevo-indicator
```

# Просмотр текущего состояния
```bash
clevo-indicator -?
```

# Установка скорости вентилятора (0-100%)
```bash
clevo-indicator 50
```

# Завершение всех экземпляров
```bash
clevo-indicator exit
```
