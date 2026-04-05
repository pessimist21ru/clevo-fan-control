# Clevo Fan Control

Интеллектуальная система управления вентиляторами для ноутбуков Clevo (N960KPx / Hasee TX9-CA5DP)

[![Version](https://img.shields.io/badge/version-1.0.2-green.svg)](https://github.com/pessimist21ru/clevo-fan-control.git)

## Особенности

- **Интеллектуальное управление** - автоматическая регулировка скорости вентиляторов на основе температур CPU/GPU/NVMe
- **Раздельные кривые** - индивидуальные настройки для CPU и GPU
- **Защита NVMe дисков** - принудительное охлаждение при 60°C (90%) и 64°C (100%)
- **Приоритет NVML** - использование точных данных NVIDIA GPU при наличии драйвера
- **Фильтрация данных** - медианный фильтр для устранения выбросов (окно 5 семплов)
- **Настраиваемое логирование** - интервал записи в systemd journal (5-3600 секунд)
- **Два режима работы**:
  - **Интерактивный режим** - ручной запуск через терминал (полный вывод в консоль)
  - **Фоновый режим (daemon)** - работа в качестве системной службы (только важные логи)
- **Автозапуск** - интеграция с systemd
- **XDG-совместимость** - конфиги и логи в стандартных директориях при работе в интерактивном режиме
- **FHS-совместимость** - конфиги и логи в системных директориях при работе в режиме фоновой службы

## Требования

- Linux с поддержкой `ioperm` (доступ к портам 0x62/0x66)
- Права root (sudo)
- CMake 3.20+
- GCC/G++ с поддержкой C++17
- Опционально: NVIDIA драйвер для точного мониторинга GPU

## Установка

### Быстрая установка (рекомендуется)

```bash
git clone https://github.com/pessimist21ru/clevo-fan-control.git
cd clevo-fan-control
chmod +x install.sh
sudo ./install.sh
```

Скрипт автоматически:
- Определит ваш дистрибутив и установит зависимости
- Проверит наличие готового бинарного файла (./compiled/ или ./build/)
- При необходимости выполнит сборку из исходников
- Создаст системные директории и настроит systemd сервис
- Предложит установить драйверы Tuxedo (опционально)

### Ручная установка из исходников

```bash
git clone https://github.com/pessimist21ru/clevo-fan-control.git
cd clevo-fan-control
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable clevo-fan-control
sudo systemctl start clevo-fan-control
```

### Проверка установки

```bash
sudo systemctl status clevo-fan-control
sudo clevo-fan-control status
```

## Использование

### Команды управления

| Команда | Описание |
|---------|----------|
| `sudo clevo-fan-control` | Запуск в нормальном режиме (интерактивный) |
| `sudo clevo-fan-control test` | Запуск с диагностическим тестом вентиляторов |
| `sudo clevo-fan-control log` | Запуск с непрерывным логированием в файл |
| `sudo clevo-fan-control status` | Показать текущий статус |
| `sudo clevo-fan-control stop` | Остановить работающий демон |
| `sudo clevo-fan-control help` | Показать справку |

### Управление через systemd

```bash
sudo systemctl start clevo-fan-control    # Запуск сервиса
sudo systemctl stop clevo-fan-control     # Остановка сервиса
sudo systemctl restart clevo-fan-control  # Перезапуск сервиса
sudo systemctl status clevo-fan-control   # Статус сервиса
sudo journalctl -u clevo-fan-control -f   # Просмотр логов в реальном времени
```

## Конфигурация

### Расположение файлов

| Режим | Конфигурация | Логи | Состояние |
|-------|--------------|------|-----------|
| **Интерактивный** | `~/.config/clevo-fan-control/` | `~/.local/share/clevo-fan-control/logs/` | `~/.local/state/clevo-fan-control/` |
| **Фоновый (служба)** | `/etc/clevo-fan-control/` | `/var/log/clevo-fan-control/` | `/var/lib/clevo-fan-control/` |

### Формат конфигурационного файла (fan_curve.conf)

```ini
[CPU]
35 0      # CPU кривая (более щадящая)
40 5
42 15
45 26
47 36
50 47
55 58
57 68
60 80
62 90
65 100

[GPU]
35 0      # GPU кривая (более агрессивная - влияет на NVMe)
40 10
42 20
45 30
47 40
50 50
55 60
57 70
60 80
62 90
65 100

[Settings]
# Интервал логирования в systemd journal (секунды, от 5 до 3600)
journal_log_interval = 600
```

*Примечание: GPU вентилятор сильнее влияет на температуру NVMe и чипсета, поэтому его кривая более агрессивная.*

## Логирование

### Режимы логирования

| Режим | Куда пишется | Частота | Когда используется |
|-------|--------------|---------|---------------------|
| **Обычный** | systemd journal | 1 раз в 10 минут (настраивается) | При работе как служба |
| **Режим `log`** | systemd journal + файл | journal: редкая, файл: каждые 0.5 сек | При диагностике проблем |

### Формат строки лога

```
[HH:MM:SS] CPU:XX | GPU:XX | GPU_EC:XX | AMB:XX | NVMe:XX/XX | FAN_CPU:XXXX/XX% | FAN_GPU:XXXX/XX% | причина
```

### Пример лога

```
[04:22:15] CPU:48 | GPU:46 | GPU_EC:43 | AMB:35 | NVMe:42/0 | FAN_CPU:1715/29% | FAN_GPU:0/0% | CPU:48°C→30% GPU:46°C→30%
```

### Просмотр логов

```bash
# systemd journal (основной)
sudo journalctl -u clevo-fan-control -f

# Файловый лог (только в режиме log)
sudo tail -f /var/log/clevo-fan-control/clevo_fan_control_*.log

# Интерактивный режим (консольный вывод)
sudo clevo-fan-control
```

## Защита NVMe

| Температура | Действие |
|-------------|----------|
| 55-60°C     | Буст скорости до +20% |
| ≥60°C       | Вентиляторы → 90% (приоритет над CPU/GPU) |
| ≥64°C       | Вентиляторы → 100% (критический режим) |

## Мониторинг

### Источники данных

| Компонент | Источник | Приоритет |
|-----------|----------|-----------|
| CPU | EC регистр 0x07 | Основной |
| GPU | NVML (NVIDIA) → EC 0xFB | NVML предпочтительнее |
| NVMe | Автоопределение через sysfs | Прямое чтение |
| Ambient | Усреднение доверенных датчиков (thermal_zones) | Расчётное |

### Фильтрация данных

- Медианный фильтр (5 семплов) - удаление единичных выбросов
- Анализ стабильности (20 семплов, 10 секунд)
- Отбрасывание скачков >30°C

## Диагностика

### Проверка EC доступа

```bash
sudo clevo-fan-control status
```

### Диагностический тест вентиляторов

```bash
sudo clevo-fan-control test
```

Тест включает:
- Противофазный тест (CPU↑ GPU↓, CPU↓ GPU↑)
- Синхронный тест (оба вентилятора вместе)
- Пошаговый тест CPU (0-100%)
- Пошаговый тест GPU (0-100%)
- Тест на конфликты (быстрые переключения)

Результаты теста сохраняются в: `/var/log/clevo-fan-control/fan_test_*.log`

## Устранение неполадок

### Конфликт с tuxedo_io/clevo_acpi

Модули `tuxedo_io` и `clevo_acpi` не мешают работе программы. Выгружать их не требуется.
Программа получает прямой доступ к оборудованию без использования драйверов Tuxedo, их установка опциональна для большей поддержки оборудования, но не является обязательной.

### NVMe диски не отображаются

Программа автоматически ищет пути к NVMe температурам при запуске. Если диски не найдены, используйте утилиту `nvme_diag` для диагностики:

```bash
cd utilites
g++ -o nvme_diag nvme_diag.cpp
sudo ./nvme_diag
```

### Задержки USB аудио

Оптимизированная версия не использует `popen()` в цикле, что исключает микро-задержки. Пути к NVMe файлам определяются один раз при старте.

## Драйверы Tuxedo

Программа опционально поддерживает установку драйверов Tuxedo для расширенной совместимости с оборудованием.

### Установка драйверов

```bash
# Во время установки через install.sh
sudo ./install.sh  # и ответьте y на вопрос о драйверах

# Или отдельно
sudo ./drivers/update-tuxedo-drivers.sh
```

### Обновление драйверов

```bash
cd drivers/tuxedo-drivers
git pull
make clean && make
sudo make modules_install
sudo depmod -a
```

## Техническая информация

### EC регистры

| Регистр | Назначение | Доступ |
|---------|------------|--------|
| 0x07 | CPU Temperature | RO |
| 0xFB | GPU Temperature (CORRECT!) | RO |
| 0xCE | CPU Fan Duty | RW (через команду 0x99) |
| 0xCF | GPU Fan Duty | RW (через команду 0x99) |
| 0xD0/D1 | CPU Fan RPM | RO |
| 0xD2/D3 | GPU Fan RPM | RO |

### EC Команды

| Команда | Порт | Назначение |
|---------|------|------------|
| 0x99 | 0x01 | Установка CPU вентилятора |
| 0x99 | 0x02 | Установка GPU вентилятора |
| 0x99 | 0xFF | Возврат в AUTO режим |

### Формула RPM

```
RPM = 2156220 / ((high_byte << 8) + low_byte)
```

## Удаление

Для полного удаления программы выполните:

```bash
cd clevo-fan-control
sudo ./uninstall.sh
```

Скрипт удаления:
- Останавливает и отключает systemd сервис
- Удаляет бинарный файл и файл сервиса
- Опционально удаляет конфигурацию и логи
- Опционально удаляет драйверы Tuxedo

## Структура проекта

```
clevo-fan-control/
├── build/                          # Директория сборки (создаётся при компиляции)
├── compiled/                       # Готовый бинарный файл (опционально)
│   └── clevo-fan-control
├── drivers/                        # Драйверы Tuxedo (опционально)
│   ├── tuxedo-drivers/             # Клонированный репозиторий драйверов
│   ├── Install_tuxedo_drivers.md
│   └── update-tuxedo-drivers.sh
├── src/
│   └── main.cpp                    # Основной исходный код
├── utilites/                       # Вспомогательные утилиты
│   ├── ec_diagnostic/              # Диагностика EC
│   ├── ec_gpu_temp_scanner/        # Поиск GPU температуры
│   ├── fan_cycle_test/             # Тест вентиляторов
│   ├── gpu_fan_test/               # Тест GPU вентилятора
│   ├── thermal_scanner/            # Сканирование тепловых датчиков
│   └── nvme_diag.cpp               # Диагностика NVMe
├── .vscode/                        # Настройки VSCode
│   ├── tasks.json                  # Задачи сборки
│   ├── settings.json               # Настройки редактора
│   └── keybindings.json            # Горячие клавиши
├── clevo-fan-control.service.in    # Прототип systemd сервиса
├── CMakeLists.txt                  # Конфигурация сборки
├── DEVELOPMENT_NOTES.md            # Заметки по разработке
├── QUICK_REFERENCE.md              # Краткая справка по EC командам
├── install.sh                      # Скрипт автоматической установки
├── uninstall.sh                    # Скрипт удаления
└── README.md                       # Документация
```

## Благодарности

- **Agramian** - оригинальный проект [clevo-fan-control](https://github.com/agramian/clevo-fan-control)
- **DeepSeek AI** - помощь в разработке, оптимизации и документировании
- Сообщество Clevo Linux - тестирование и обратная связь

## Автор

Guryev Pavel (pilatnet@gmail.com)

---

**Версия 1.0.2** - Финальный релиз с интеллектуальным управлением вентиляторами, защитой NVMe и оптимизированным логированием.