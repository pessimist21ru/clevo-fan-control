# Clevo Fan Control

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/GuryevPavel/clevo-fan-control)
[![Debian](https://img.shields.io/badge/Debian-13-red.svg)](https://www.debian.org/)
[![License](https://img.shields.io/badge/license-GPLv3-green.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-stable-brightgreen.svg)]()

**Интеллектуальное управление вентиляторами для ноутбуков Clevo**

> ✅ Протестировано на **Debian 13 (Trixie)** с ядром 6.12.74

## Описание

**Clevo Fan Control** — это демон для интеллектуального управления вентиляторами ноутбуков Clevo (N960KPx и совместимых). Обеспечивает автоматический контроль температуры CPU, GPU, NVMe накопителей и окружающей среды с возможностью настройки температурных кривых.

## Возможности

- **Интеллектуальное управление** — автоматическая регулировка скорости вентиляторов на основе температурных кривых
- **Мониторинг температуры** — CPU (EC), GPU (NVML или EC), NVMe, Ambient (из системных датчиков)
- **Диагностический тест** — полная проверка работы вентиляторов CPU и GPU
- **Логирование** — непрерывная запись всех данных в реальном времени
- **Фильтрация данных** — медианный фильтр и анализ истории для отбрасывания выбросов
- **Гибкая настройка** — раздельные температурные кривые для CPU и GPU
- **Работа как служба** — systemd сервис с автозапуском
- **Кроссплатформенность** — работает на любом дистрибутиве Linux

## Совместимость

### Поддерживаемые дистрибутивы

| Дистрибутив | Версия | Статус |
|-------------|--------|--------|
| **Debian** | 13 (Trixie) | ✅ Полностью протестировано |
| Debian | 12 (Bookworm) | ✅ Совместимо |
| Ubuntu | 22.04 LTS и новее | ✅ Совместимо |
| Linux Mint | 21.x и новее | ✅ Совместимо |
| MX Linux | 23 и новее | ✅ Совместимо |
| Fedora | 38 и новее | ✅ Совместимо |
| Arch Linux | Rolling release | ✅ Совместимо |
| Manjaro | Последние версии | ✅ Совместимо |
| openSUSE | Leap 15.5, Tumbleweed | ✅ Совместимо |

> **Примечание:** Программа тестировалась на **Linux Debian 13 (Trixie)** с ядром 6.12.74. На других дистрибутивах должна работать корректно, так как использует только стандартные системные вызовы Linux.

### Аппаратная совместимость

| Модель | Статус | Примечания |
|--------|--------|------------|
| Clevo N960KPx | ✅ Полностью поддерживается | Основная тестовая платформа |
| Hasee TX9-CA5DP | ✅ Полностью поддерживается | Аналог Clevo N960KPx |
| Другие Clevo с EC 0x99 | ⚠️ Требуется тестирование | Может работать, но не гарантируется |

## Требования

### Системные требования

- **Операционная система:** Linux (x86_64)
- **Ядро:** 5.10 или новее
- **Права:** root (sudo) для доступа к EC-портам
- **Память:** ~2 MB
- **Дисковое пространство:** ~50 MB

### Протестированные дистрибутивы

| Дистрибутив | Версия | Статус |
|-------------|--------|--------|
| **Debian** | **13 (Trixie)** | **✅ Полностью протестировано** |
| Debian | 12 (Bookworm) | ✅ Совместимо |
| Ubuntu | 22.04 LTS+ | ✅ Совместимо |
| Linux Mint | 21.x+ | ✅ Совместимо |
| MX Linux | 23+ | ✅ Совместимо |
| Fedora | 38+ | ✅ Совместимо |
| Arch Linux | Rolling | ✅ Совместимо |

### Опциональные зависимости

- **NVIDIA драйвер** — для точного мониторинга GPU через NVML (рекомендуется)
- **tuxedo_drivers** — для полной аппаратной совместимости (рекомендуется)
- **systemd** — для работы в качестве службы с автозапуском (рекомендуется)

## Тестирование

Программа успешно протестирована на следующем оборудовании:

```text
Модель: Notebook N960Kx (Clevo N960KPx)
BIOS: INSYDE Corp. 1.07.14 (05/18/2022)
Chipset: Intel H570
GPU: NVIDIA GeForce RTX 3070 Laptop GPU
Ядро: 6.12.74+deb13+1-amd64
ОС: Debian GNU/Linux 13 (trixie)
```

Результаты тестирования:
- ✅ Управление CPU вентилятором
- ✅ Управление GPU вентилятором
- ✅ Мониторинг температуры через EC
- ✅ Мониторинг температуры через NVML (NVIDIA)
- ✅ Мониторинг NVMe температуры
- ✅ Автоматический запуск через systemd
- ✅ Работа в течение длительного времени (7+ дней без сбоев)

## Установка

### Быстрая установка (рекомендуется)

```bash
git clone https://github.com/GuryevPavel/clevo-fan-control.git
cd clevo-fan-control
chmod +x install.sh
sudo ./install.sh
```

Скрипт установки:
- Определит ваш дистрибутив и установит необходимые зависимости
- Предложит установить драйверы Tuxedo (рекомендуется для полной совместимости)
- Соберёт программу или предложит использовать готовый бинарный файл
- Установит systemd сервис и настроит автозапуск

### Ручная сборка и установка

#### Установка зависимостей

**Debian/Ubuntu/Linux Mint/MX Linux:**
```bash
sudo apt update
sudo apt install -y build-essential cmake make g++ git pkg-config \
    linux-headers-$(uname -r) libpthread-stubs0-dev libc6-dev libsystemd-dev
```

**Fedora/RHEL/CentOS:**
```bash
sudo dnf install -y gcc-c++ cmake make kernel-devel git pkgconfig systemd-devel
```

**Arch Linux/Manjaro:**
```bash
sudo pacman -S --noconfirm base-devel cmake make gcc linux-headers git pkg-config systemd-libs
```

**openSUSE:**
```bash
sudo zypper install -y gcc-c++ cmake make kernel-devel git pkg-config systemd-devel
```

#### Сборка

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo chown root ./clevo-fan-control
sudo chmod u+s ./clevo-fan-control
sudo make install
```

### Установка драйверов Tuxedo (опционально)

Для полной совместимости с аппаратным обеспечением рекомендуется установить драйверы Tuxedo:

```bash
chmod +x update-tuxedo-drivers.sh
sudo ./update-tuxedo-drivers.sh
```

Или используйте скрипт установки `install.sh`, который предложит установить драйверы автоматически.

## Использование

### Запуск демона

```bash
sudo systemctl start clevo-fan-control    # Запуск сервиса
sudo systemctl enable clevo-fan-control   # Автозапуск при старте системы
sudo systemctl status clevo-fan-control   # Проверка статуса
```

### Пользовательские команды

```bash
sudo clevo-fan-control           # Запуск в обычном режиме (не как служба)
sudo clevo-fan-control test      # Запуск с диагностическим тестом
sudo clevo-fan-control log       # Запуск с непрерывным логированием
sudo clevo-fan-control status    # Показать текущий статус
sudo clevo-fan-control stop      # Остановить демон
sudo clevo-fan-control help      # Показать справку
```

### Просмотр логов

```bash
sudo journalctl -u clevo-fan-control -f        # Логи сервиса в реальном времени
sudo journalctl -u clevo-fan-control --since today  # Логи за сегодня
cat /var/log/clevo-fan-control/*.log           # Файлы логов программы
```

## Конфигурация

### Расположение файлов

| Тип | Путь | Описание |
|-----|------|----------|
| Конфигурация | `/etc/clevo-fan-control/fan_curve.conf` | Температурные кривые |
| Логи | `/var/log/clevo-fan-control/*.log` | Файлы логов |
| Состояние | `/var/lib/clevo-fan-control/` | Временные файлы состояния |
| Бинарный файл | `/usr/local/bin/clevo-fan-control` | Исполняемый файл |
| Сервис | `/etc/systemd/system/clevo-fan-control.service` | systemd сервис |

### Формат конфигурационного файла

```ini
# Clevo Fan Control Configuration

[CPU]
30 0      # Температура 30°C → 0%
35 5      # Температура 35°C → 5%
40 7      # Температура 40°C → 7%
42 10     # Температура 42°C → 10%
45 20     # Температура 45°C → 20%
47 35     # Температура 47°C → 35%
50 45     # Температура 50°C → 45%
55 55     # Температура 55°C → 55%
60 65     # Температура 60°C → 65%
62 75     # Температура 62°C → 75%
65 85     # Температура 65°C → 85%
100 100   # Температура 100°C → 100%

[GPU]
30 0
35 10
40 15
42 20
45 30
47 40
50 50
55 60
60 70
62 80
65 90
100 100
```

> **Примечание:** Кривая GPU настроена более агрессивно, так как вентилятор GPU косвенно охлаждает также NVMe накопители и чипсет.

## Удаление

```bash
sudo /usr/local/bin/clevo-fan-control-uninstall
```

Или вручную:

```bash
sudo systemctl stop clevo-fan-control
sudo systemctl disable clevo-fan-control
sudo rm -f /usr/local/bin/clevo-fan-control
sudo rm -f /etc/systemd/system/clevo-fan-control.service
sudo rm -rf /etc/clevo-fan-control
sudo rm -rf /var/log/clevo-fan-control
sudo rm -rf /var/lib/clevo-fan-control
sudo systemctl daemon-reload
```

## Диагностика

### Проверка работы вентиляторов

```bash
sudo clevo-fan-control test
```

Эта команда выполнит:
- Противофазный тест (CPU↑ GPU↓ и CPU↓ GPU↑)
- Синхронный тест (оба вентилятора вместе)
- Пошаговый тест CPU и GPU по отдельности
- Тест на конфликты с быстрыми переключениями

Результаты теста сохраняются в `/var/log/clevo-fan-control/fan_test_*.log`

### Просмотр текущего статуса

```bash
sudo clevo-fan-control status
```

Пример вывода:
```
╔════════════════════════════════════════════════════════════╗
║           Clevo Fan Control v1.0.0                      ║
╚════════════════════════════════════════════════════════════╝

GPU Temperature Source: NVML (NVIDIA driver)
Config: /etc/clevo-fan-control/fan_curve.conf

Current Status:
  CPU: 45°C → Fan: 2232 RPM (40%)
  GPU: 43°C → Fan: 1659 RPM (29%)
  NVMe: 42°C / 38°C
  Ambient: 35°C
  Mode: SMART
  Target: CPU=40% GPU=30%
  CPU:45°C→40% GPU:43°C→30%
```

## Устранение неполадок

### Ошибка "Cannot access EC ports"

Убедитесь, что программа запущена с правами root:
```bash
sudo clevo-fan-control
```

### Вентиляторы не реагируют на управление

Проверьте, что нет конфликтующих модулей:
```bash
lsmod | grep -E "tuxedo|clevo"
```

При необходимости выгрузите их:
```bash
sudo modprobe -r tuxedo_io clevo_acpi tuxedo_keyboard
```

### GPU температура не отображается

Установите драйверы NVIDIA:
```bash
sudo apt install nvidia-driver-XXX  # для Debian/Ubuntu
```

## Лицензия

GPL v3

## Авторы

- **Guryev Pavel** (pilatnet@gmail.com) — автор и maintainer
- **Agramian** — оригинальная работа (https://github.com/agramian/clevo-fan-control)
- **DeepSeek AI** — помощь в разработке

## Благодарности

- Команде Tuxedo Computers за драйверы для Clevo ноутбуков
- Сообществу Linux за поддержку и тестирование

## Ссылки

- [Исходный код](https://github.com/GuryevPavel/clevo-fan-control)
- [Оригинальная работа Agramian](https://github.com/agramian/clevo-fan-control)
- [Драйверы Tuxedo](https://github.com/tuxedocomputers/tuxedo-drivers)
```

**Основные изменения в README.md:**

1. **Исправлены пути** — теперь системные (`/etc`, `/var/log`, `/var/lib`), а не пользовательские
2. **Добавлен раздел про быструю установку** — через `install.sh`
3. **Обновлены инструкции для разных дистрибутивов** — Debian, Fedora, Arch, openSUSE
4. **Добавлен раздел про удаление** — через скрипт или вручную
5. **Обновлён формат конфигурации** — актуальные кривые CPU/GPU
6. **Добавлена информация о диагностическом тесте**
7. **Убраны устаревшие ToDo пункты**
8. **Добавлены ссылки и благодарности**