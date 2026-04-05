/*
============================================================================
 Name        : clevo-fan-control.cpp
 Author      : Guryev Pavel (pilatnet@gmail.com)
 Version     : 1.0.2
 Description : Clevo fan control daemon
============================================================================

 Based on original work by Agramian:
   https://github.com/agramian/clevo-fan-control

 Enhanced with intelligent fan control, diagnostic testing,
 and FHS/XDG-compliant paths.

 This utility was developed with assistance from DeepSeek AI.
 
============================================================================
*/

#define VERSION "1.0.2"
#define AUTHOR "Guryev Pavel (pilatnet@gmail.com)"
#define ORIGINAL_AUTHOR "Agramian (https://github.com/agramian/clevo-fan-control)"
#define DEEPSEEK_CREDIT "Developed with assistance from DeepSeek AI"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <glob.h>
#include <libgen.h>
#include <pwd.h>

//============================================================================
// Константы
//============================================================================

#define EC_SC 0x66
#define EC_DATA 0x62
#define IBF 1
#define OBF 0
#define EC_SC_READ_CMD 0x80

// EC Commands
#define EC_CMD_FAN 0x99
#define EC_PORT_CPU 0x01
#define EC_PORT_GPU 0x02
#define AUTO_FAN_VALUE 0xFF

// EC Registers
#define EC_REG_CPU_TEMP     0x07
#define EC_REG_GPU_TEMP     0xFB
#define EC_REG_FAN_CPU_DUTY 0xCE
#define EC_REG_FAN_CPU_RPMS_HI 0xD0
#define EC_REG_FAN_CPU_RPMS_LO 0xD1
#define EC_REG_FAN_GPU_DUTY 0xCF
#define EC_REG_FAN_GPU_RPMS_HI 0xD2
#define EC_REG_FAN_GPU_RPMS_LO 0xD3

#define MIN_FAN_DUTY 0
#define MAX_FAN_DUTY 100

// Параметры фильтрации
#define MEDIAN_WINDOW 5
#define HISTORY_SIZE 20
#define TEMP_MIN_VALID 20
#define TEMP_MAX_VALID 100
#define UPDATE_INTERVAL_US 500000
#define AMBIENT_SENSORS_MAX 32

// Параметры диагностического теста
#define TEST_STEP_DURATION 3

// Пороги NVMe
#define NVME_WARNING_TEMP 60
#define NVME_CRITICAL_TEMP 64

//============================================================================
// Структуры данных
//============================================================================

struct SensorInfo {
    char name[64];
    char path[256];
    int temp;
    int valid;
    int calibration_readings[10];
    int cal_idx;
    int trusted;
};

struct FanCurvePoint {
    int temp;
    int speed;
};

struct FanCurve {
    struct FanCurvePoint points[20];
    int num_points;
};

struct Config {
    struct FanCurve cpu_curve;
    struct FanCurve gpu_curve;
    int median_window;
    int history_size;
    int update_interval_ms;
    int emergency_temp;
    int journal_log_interval_sec;
};

struct MedianFilter {
    int buffer[MEDIAN_WINDOW];
    int index;
    int filled;
};

struct TemperatureHistory {
    int samples[HISTORY_SIZE];
    int index;
    int filled;
};

struct FanInfo {
    int duty;
    int rpms;
};

struct SharedInfo {
    volatile int exit;
    volatile int cpu_temp_raw;
    volatile int cpu_temp_filtered;
    volatile int gpu_temp_raw;
    volatile int gpu_temp_filtered;
    volatile int ambient_temp;
    volatile int nvme1_temp;
    volatile int nvme2_temp;
    struct FanInfo cpu_fan;
    struct FanInfo gpu_fan;
    volatile int smart_mode;
    volatile int sync_fans;
    volatile int emergency;
    volatile int cpu_target;
    volatile int gpu_target;
    char reason[256];
    volatile int nvidia_available;
    volatile int nvidia_temp;
    volatile int diagnostic_mode;
};

//============================================================================
// Глобальные переменные
//============================================================================

static int interactive_mode = 0;  // 0 - служба (тихий режим), 1 - интерактивный
static SharedInfo *share_info = NULL;
static void *nvidia_handle = NULL;
static int nvidia_nvml_available = 0;
static pthread_t worker_thread;
static FILE *log_file = NULL;
static int file_logging_enabled = 0;  // Флаг: 0 - только journal, 1 - журнал + файл
static FILE *test_log = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct Config config;
static char config_path[512];
static char log_dir[512];
static char test_log_dir[512];
static char state_dir[512];

// Фильтры
static struct MedianFilter cpu_filter;
static struct MedianFilter gpu_filter;
static struct TemperatureHistory cpu_history;
static struct TemperatureHistory gpu_history;

// Датчики для Ambient
static struct SensorInfo ambient_sensors[AMBIENT_SENSORS_MAX];
static int ambient_sensor_count = 0;
static int ambient_last_valid = 35;

//============================================================================
// Прототипы функций
//============================================================================

static void print_help(void);
static void console_printf(const char* format, ...);
static void create_path(const char* path);
static void get_paths(int argc, char *argv[]);
static void init_default_config(void);
static int load_config(void);
static int save_config(void);

static void init_median_filter(struct MedianFilter *filter);
static void add_to_median_filter(struct MedianFilter *filter, int value);
static int get_median_value(struct MedianFilter *filter);
static void init_history(struct TemperatureHistory *history);
static void add_to_history(struct TemperatureHistory *history, int value);
static int is_stable(struct TemperatureHistory *history);
static int validate_temperature(int temp, int last_valid);
static int filter_temperature(int raw_temp, struct MedianFilter *filter, 
                              struct TemperatureHistory *history, int *last_valid);

static int ec_init(void);
static int ec_wait(uint32_t port, uint32_t flag, int value);
static uint8_t ec_read(uint8_t reg);
static int ec_write(uint8_t cmd, uint8_t port, uint8_t value);
static void ec_set_auto_mode(void);
static void ec_read_fan_status(void);
static int ec_write_cpu_fan(int duty);
static int ec_write_gpu_fan(int duty);
static int ec_write_both_fans(int cpu, int gpu);
static int ec_read_cpu_temp(void);
static int ec_read_gpu_temp(void);

static void scan_ambient_sensors(void);
static int calculate_ambient_temp(void);
static void update_ambient_temp(void);

static void nvidia_init(void);
static void nvidia_update(void);
static void scan_nvme_disks(void);
static void update_nvme_temps(void);
static void log_sensor_data(void);
static int get_fan_speed_from_curve(struct FanCurve *curve, int temp);
static void smart_control_update(void);

static void init_log_files(void);
static void* worker_thread_func(void*);
static void signal_handler(int sig);
static void print_status(void);

static void test_fan_pair(int cpu_speed, int gpu_speed, const char* description);
static void test_fan_pair_phase(int cpu_start, int gpu_start, int cpu_end, int gpu_end, const char* description);
static void test_single_fan(int fan_type, int speed, const char* description);
static void log_test_result(const char* test_name, int cpu_rpm, int gpu_rpm, int cpu_duty, int gpu_duty);
static void run_diagnostic_test(void);

//============================================================================
// Вспомогательные функции
//============================================================================

static void print_help(void) {
    console_printf("\n");
    console_printf("╔══════════════════════════════════════════════════════════════════╗\n");
    console_printf("║           Clevo Fan Control v%s                              ║\n", VERSION);
    console_printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
    console_printf("DESCRIPTION:\n");
    console_printf("  Intelligent fan control daemon for Clevo laptops (N960KPx).\n");
    console_printf("  Monitors CPU/GPU/NVMe temperatures and adjusts fan speeds\n");
    console_printf("  according to configurable fan curves.\n\n");
    console_printf("  NVMe protection:\n");
    console_printf("    - 55-60°C: Speed boost up to +20%%\n");
    console_printf("    - ≥60°C: Fans → 90%% (overrides CPU/GPU control)\n");
    console_printf("    - ≥64°C: Fans → 100%% (critical)\n\n");
    console_printf("AUTHOR:\n");
    console_printf("  %s\n", AUTHOR);
    console_printf("  Based on original work by %s\n", ORIGINAL_AUTHOR);
    console_printf("  %s\n\n", DEEPSEEK_CREDIT);
    console_printf("USAGE:\n");
    console_printf("  sudo %s [OPTIONS]\n\n", program_invocation_short_name);
    console_printf("OPTIONS:\n");
    console_printf("  (no options)  - Start daemon in normal operation mode\n");
    console_printf("  test          - Run diagnostic fan test, then start daemon\n");
    console_printf("  status        - Show current fan and temperature status\n");
    console_printf("  stop          - Stop running daemon, return control to system\n");
    console_printf("  log           - Start daemon with logging to file\n");
    console_printf("  help          - Show this help message\n\n");
    console_printf("CONFIGURATION:\n");
    console_printf("  Config file: %s/fan_curve.conf\n", config_path);
    console_printf("  Log files:   %s/\n", log_dir);
    console_printf("FAN CURVE FORMAT:\n");
    console_printf("  [CPU]\n");
    console_printf("  35 0      # CPU curve (more conservative)\n");
    console_printf("  40 5\n");
    console_printf("  42 15\n");
    console_printf("  45 26\n");
    console_printf("  47 36\n");
    console_printf("  50 47\n");
    console_printf("  55 58\n");
    console_printf("  57 68\n");
    console_printf("  60 80\n");
    console_printf("  62 90\n");
    console_printf("  65 100\n");
    console_printf("  ...\n");
    console_printf("  [GPU]\n");
    console_printf("  35 0      # GPU curve (more aggressive - affects NVMe)\n");
    console_printf("  40 10\n");
    console_printf("  42 20\n");
    console_printf("  45 30\n");
    console_printf("  47 40\n");
    console_printf("  50 50\n");
    console_printf("  55 60\n");
    console_printf("  57 70\n");
    console_printf("  60 80\n");
    console_printf("  62 90\n");
    console_printf("  65 100\n\n");
    console_printf("EXAMPLES:\n");
    console_printf("  sudo %s           # Start daemon (no test)\n", program_invocation_short_name ? program_invocation_short_name : "clevo-fan-control");
    console_printf("  sudo %s test      # Run diagnostic test, then start daemon\n", program_invocation_short_name ? program_invocation_short_name : "clevo-fan-control");
    console_printf("  sudo %s status    # Check current status\n", program_invocation_short_name ? program_invocation_short_name : "clevo-fan-control");
    console_printf("  sudo %s stop      # Stop daemon\n", program_invocation_short_name ? program_invocation_short_name : "clevo-fan-control");
    console_printf("  sudo %s log       # Start daemon with logging\n\n", program_invocation_short_name ? program_invocation_short_name : "clevo-fan-control");
}

static void console_printf(const char* format, ...) {
    if (!interactive_mode) return;  // В режиме службы ничего не выводим в консоль
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

//============================================================================
// XDG/FHS-совместимые пути
//============================================================================

// Функция для безопасного создания пути
static void create_path(const char* path) {
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "Warning: Attempt to create empty path\n");
        return;
    }
    
    char buf[512];
    size_t len = strlen(path);
    
    if (len >= sizeof(buf)) {
        fprintf(stderr, "Error: Path too long: %s\n", path);
        return;
    }
    
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    char *p = buf;
    
    // Пропускаем первый слеш (корневая директория)
    if (*p == '/') {
        p++;
    }
    
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            
            // Пропускаем создание корневой директории
            if (buf[0] != '\0' && !(buf[0] == '/' && buf[1] == '\0')) {
                struct stat st;
                if (stat(buf, &st) == 0) {
                    if (!S_ISDIR(st.st_mode)) {
                        fprintf(stderr, "Warning: %s exists but is not a directory\n", buf);
                    }
                } else {
                    if (mkdir(buf, 0755) != 0) {
                        if (errno != EEXIST) {
                            fprintf(stderr, "Warning: Cannot create directory %s: %s\n", 
                                    buf, strerror(errno));
                        }
                    }
                }
            }
            
            *p = '/';
        }
        p++;
    }
    
    // Создаём финальную директорию (только если путь не пустой)
    if (buf[0] != '\0') {
        struct stat st;
        if (stat(buf, &st) == 0) {
            if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "Warning: %s exists but is not a directory\n", buf);
            }
        } else {
            if (mkdir(buf, 0755) != 0) {
                if (errno != EEXIST) {
                    fprintf(stderr, "Warning: Cannot create directory %s: %s\n", 
                            buf, strerror(errno));
                }
            }
        }
    }
}

static void get_paths(int argc, char *argv[]) {
    int system_mode = 0;
    
    if (geteuid() == 0 && getenv("SUDO_USER") == NULL) {
        system_mode = 1;
    }
    
    if (getenv("INVOCATION_ID") != NULL) {
        system_mode = 1;
        interactive_mode = 0;  //запущено как служба
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--system") == 0) {
            system_mode = 1;
            break;
        }
    }
    
    if (system_mode) {
        snprintf(config_path, sizeof(config_path), "/etc/clevo-fan-control");
        snprintf(log_dir, sizeof(log_dir), "/var/log/clevo-fan-control");
        snprintf(state_dir, sizeof(state_dir), "/var/lib/clevo-fan-control");
        snprintf(test_log_dir, sizeof(test_log_dir), "/var/log/clevo-fan-control");
        
        create_path(config_path);
        create_path(log_dir);
        create_path(state_dir);
        
        console_printf("✓ Running in SYSTEM mode (service)\n");
        console_printf("✓ Config: %s\n", config_path);
        console_printf("✓ Logs: %s\n", log_dir);
        
    } else {
        interactive_mode = 1;  //интерактивный режим
        
        uid_t uid = getuid();
        const char *sudo_user = getenv("SUDO_USER");
        struct passwd *pw = NULL;
        
        if (sudo_user && strlen(sudo_user) > 0) {
            pw = getpwnam(sudo_user);
        }
        
        if (!pw) {
            pw = getpwuid(uid);
        }
        
        const char *home = pw ? pw->pw_dir : getenv("HOME");
        if (!home) home = "/root";
        
        const char *username = pw ? pw->pw_name : "root";
        
        const char *xdg_config = getenv("XDG_CONFIG_HOME");
        if (xdg_config && strstr(xdg_config, home)) {
            snprintf(config_path, sizeof(config_path), "%s/clevo-fan-control", xdg_config);
        } else {
            snprintf(config_path, sizeof(config_path), "%s/.config/clevo-fan-control", home);
        }
        
        const char *xdg_data = getenv("XDG_DATA_HOME");
        if (xdg_data && strstr(xdg_data, home)) {
            snprintf(log_dir, sizeof(log_dir), "%s/clevo-fan-control/logs", xdg_data);
        } else {
            snprintf(log_dir, sizeof(log_dir), "%s/.local/share/clevo-fan-control/logs", home);
        }
        
        const char *xdg_state = getenv("XDG_STATE_HOME");
        if (xdg_state && strstr(xdg_state, home)) {
            snprintf(state_dir, sizeof(state_dir), "%s/clevo-fan-control", xdg_state);
        } else {
            snprintf(state_dir, sizeof(state_dir), "%s/.local/state/clevo-fan-control", home);
        }
        
        snprintf(test_log_dir, sizeof(test_log_dir), "%s", log_dir);
        
        uid_t euid = geteuid();
        if (euid == 0 && pw) {
            seteuid(pw->pw_uid);
        }
        
        create_path(config_path);
        create_path(log_dir);
        create_path(state_dir);
        
        if (euid == 0) {
            seteuid(0);
        }
        
        console_printf("✓ Running in USER mode (interactive)\n");
        console_printf("✓ User: %s\n", username);
        console_printf("✓ Config: %s\n", config_path);
        console_printf("✓ Logs: %s\n", log_dir);
    }
}


//============================================================================
// Конфигурация
//============================================================================

static void init_default_config(void) {

    struct FanCurvePoint cpu_default[] = {
        {35, 0}, {40, 5}, {42, 15}, {45, 26}, {47, 36},
        {50, 47}, {55, 58}, {57, 68}, {60, 80}, {62, 90}, {65, 100}
    };

    struct FanCurvePoint gpu_default[] = {
        {35, 0}, {40, 10}, {42, 20}, {45, 30}, {47, 40},
        {50, 50}, {55, 60}, {57, 70}, {60, 80}, {62, 90}, {65, 100}
    };

    config.cpu_curve.num_points = sizeof(cpu_default) / sizeof(cpu_default[0]);
    memcpy(config.cpu_curve.points, cpu_default, sizeof(cpu_default));
    
    config.gpu_curve.num_points = sizeof(gpu_default) / sizeof(gpu_default[0]);
    memcpy(config.gpu_curve.points, gpu_default, sizeof(gpu_default));
    
    config.median_window = MEDIAN_WINDOW;
    config.history_size = HISTORY_SIZE;
    config.update_interval_ms = UPDATE_INTERVAL_US / 1000;
    config.emergency_temp = 85;
    config.journal_log_interval_sec = 600;
}

static int load_config(void) {
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/fan_curve.conf", config_path);
    
    FILE *f = fopen(filename, "r");
    if (!f) {
        save_config();
        return 0;
    }
    
    char line[256];
    int section = 0;
    int valid = 1;
    
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '#') continue;
        
        char *end = p + strlen(p) - 1;
        while (end > p && (*end == '\n' || *end == ' ' || *end == '\t')) *end-- = '\0';
        
        if (strcmp(p, "[CPU]") == 0) {
            section = 1;
            config.cpu_curve.num_points = 0;
        } else if (strcmp(p, "[GPU]") == 0) {
            section = 2;
            config.gpu_curve.num_points = 0;
        } else if (strcmp(p, "[Settings]") == 0) {  // ← НОВАЯ СЕКЦИЯ
            section = 3;
        } else if (section == 1 && config.cpu_curve.num_points < 20) {
            int temp, speed;
            if (sscanf(p, "%d %d", &temp, &speed) == 2) {
                if (temp >= 0 && temp <= 100 && speed >= 0 && speed <= 100) {
                    config.cpu_curve.points[config.cpu_curve.num_points].temp = temp;
                    config.cpu_curve.points[config.cpu_curve.num_points].speed = speed;
                    config.cpu_curve.num_points++;
                } else valid = 0;
            }
        } else if (section == 2 && config.gpu_curve.num_points < 20) {
            int temp, speed;
            if (sscanf(p, "%d %d", &temp, &speed) == 2) {
                if (temp >= 0 && temp <= 100 && speed >= 0 && speed <= 100) {
                    config.gpu_curve.points[config.gpu_curve.num_points].temp = temp;
                    config.gpu_curve.points[config.gpu_curve.num_points].speed = speed;
                    config.gpu_curve.num_points++;
                } else valid = 0;
            }
        } else if (section == 3) {
            // Читаем параметры настроек
            int interval;
            if (sscanf(p, "journal_log_interval = %d", &interval) == 1) {
                if (interval >= 5 && interval <= 3600) {  // от 5 секунд до 1 часа
                    config.journal_log_interval_sec = interval;
                }
            }
        }
    }
    fclose(f);
    
    if (!valid || config.cpu_curve.num_points == 0 || config.gpu_curve.num_points == 0) {
        init_default_config();
        save_config();
        return 0;
    }
    
    return 1;
}

static int save_config(void) {
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/fan_curve.conf", config_path);
    
    FILE *f = fopen(filename, "w");
    if (!f) return 0;
    
    fprintf(f, "# Clevo Fan Control Configuration\n");
    fprintf(f, "# Generated automatically\n\n");
    
    fprintf(f, "[CPU]\n");
    for (int i = 0; i < config.cpu_curve.num_points; i++) {
        fprintf(f, "%d %d\n", config.cpu_curve.points[i].temp, config.cpu_curve.points[i].speed);
    }
    
    fprintf(f, "\n[GPU]\n");
    for (int i = 0; i < config.gpu_curve.num_points; i++) {
        fprintf(f, "%d %d\n", config.gpu_curve.points[i].temp, config.gpu_curve.points[i].speed);
    }
    
    // ← ДОБАВИТЬ СЕКЦИЮ НАСТРОЕК
    fprintf(f, "\n[Settings]\n");
    fprintf(f, "# Interval for journal logging (seconds, 5-3600)\n");
    fprintf(f, "journal_log_interval = %d\n", config.journal_log_interval_sec);
    
    fclose(f);
    return 1;
}

//============================================================================
// Фильтры
//============================================================================

static void init_median_filter(struct MedianFilter *filter) {
    memset(filter->buffer, 0, sizeof(filter->buffer));
    filter->index = 0;
    filter->filled = 0;
}

static void add_to_median_filter(struct MedianFilter *filter, int value) {
    filter->buffer[filter->index] = value;
    filter->index = (filter->index + 1) % config.median_window;
    if (filter->filled < config.median_window) filter->filled++;
}

static int compare_int(const void *a, const void *b) {
    return *(int*)a - *(int*)b;
}

static int get_median_value(struct MedianFilter *filter) {
    if (filter->filled == 0) return 0;
    int sorted[MEDIAN_WINDOW];
    memcpy(sorted, filter->buffer, sizeof(sorted));
    qsort(sorted, filter->filled, sizeof(int), compare_int);
    return sorted[filter->filled / 2];
}

static void init_history(struct TemperatureHistory *history) {
    memset(history->samples, 0, sizeof(history->samples));
    history->index = 0;
    history->filled = 0;
}

static void add_to_history(struct TemperatureHistory *history, int value) {
    history->samples[history->index] = value;
    history->index = (history->index + 1) % config.history_size;
    if (history->filled < config.history_size) history->filled++;
}

static int is_stable(struct TemperatureHistory *history) {
    if (history->filled < config.history_size) return 0;
    int sum = 0;
    for (int i = 0; i < history->filled; i++) sum += history->samples[i];
    int avg = sum / history->filled;
    int variance = 0;
    for (int i = 0; i < history->filled; i++) {
        int diff = history->samples[i] - avg;
        variance += diff * diff;
    }
    int stddev = sqrt(variance / history->filled);
    return stddev < 3;
}

static int validate_temperature(int temp, int last_valid) {
    if (temp < TEMP_MIN_VALID || temp > TEMP_MAX_VALID) return 0;
    if (last_valid > 0 && abs(temp - last_valid) > 30) return 0;
    return 1;
}

static int filter_temperature(int raw_temp, struct MedianFilter *filter, 
                              struct TemperatureHistory *history, int *last_valid) {
    add_to_median_filter(filter, raw_temp);
    int median_temp = get_median_value(filter);
    
    if (!validate_temperature(median_temp, *last_valid)) {
        return *last_valid;
    }
    
    add_to_history(history, median_temp);
    if (is_stable(history)) {
        *last_valid = median_temp;
    }
    return median_temp;
}

//============================================================================
// Ambient температура (из системных датчиков)
//============================================================================

static void scan_ambient_sensors(void) {
    console_printf("\n🔍 Scanning ambient temperature sensors...\n");
    ambient_sensor_count = 0;
    
    for (int zone = 0; zone < 30 && ambient_sensor_count < AMBIENT_SENSORS_MAX; zone++) {
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", zone);
        
        FILE *f = fopen(path, "r");
        if (!f) continue;
        
        int temp = 0;
        if (fscanf(f, "%d", &temp) != 1) {
            fclose(f);
            continue;
        }
        fclose(f);
        
        char type_path[256];
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/thermal_zone%d/type", zone);
        
        char type[64] = "unknown";
        FILE *tf = fopen(type_path, "r");
        if (tf) {
            if (fgets(type, sizeof(type), tf)) {
                type[strcspn(type, "\n")] = 0;
            }
            fclose(tf);
        }
        
        snprintf(ambient_sensors[ambient_sensor_count].name, 
                 sizeof(ambient_sensors[ambient_sensor_count].name), "thermal_zone%d", zone);
        snprintf(ambient_sensors[ambient_sensor_count].path, 
                 sizeof(ambient_sensors[ambient_sensor_count].path), "%s", path);
        ambient_sensors[ambient_sensor_count].temp = temp / 1000;
        ambient_sensors[ambient_sensor_count].valid = 0;
        ambient_sensors[ambient_sensor_count].cal_idx = 0;
        ambient_sensor_count++;
    }
    
    for (int hwmon = 0; hwmon < 20 && ambient_sensor_count < AMBIENT_SENSORS_MAX; hwmon++) {
        char name_path[256];
        snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/hwmon%d/name", hwmon);
        
        FILE *nf = fopen(name_path, "r");
        if (!nf) continue;
        
        char hwmon_name[64];
        if (!fgets(hwmon_name, sizeof(hwmon_name), nf)) {
            fclose(nf);
            continue;
        }
        hwmon_name[strcspn(hwmon_name, "\n")] = 0;
        fclose(nf);
        
        if (strstr(hwmon_name, "acpitz") || strstr(hwmon_name, "iwlwifi")) {
            char temp_path[256];
            snprintf(temp_path, sizeof(temp_path), "/sys/class/hwmon/hwmon%d/temp1_input", hwmon);
            
            FILE *tf = fopen(temp_path, "r");
            if (!tf) continue;
            
            int temp = 0;
            if (fscanf(tf, "%d", &temp) != 1) {
                fclose(tf);
                continue;
            }
            fclose(tf);
            
            snprintf(ambient_sensors[ambient_sensor_count].name, 
                     sizeof(ambient_sensors[ambient_sensor_count].name), "hwmon%d", hwmon);
            snprintf(ambient_sensors[ambient_sensor_count].path, 
                     sizeof(ambient_sensors[ambient_sensor_count].path), "%s", temp_path);
            ambient_sensors[ambient_sensor_count].temp = temp / 1000;
            ambient_sensors[ambient_sensor_count].valid = 0;
            ambient_sensors[ambient_sensor_count].cal_idx = 0;
            ambient_sensor_count++;
        }
    }
    
    console_printf("  Found %d sensors for ambient temperature\n", ambient_sensor_count);
    
    console_printf("  Calibrating sensors (5 seconds)...");
    fflush(stdout);
    
    for (int sec = 0; sec < 5; sec++) {
        for (int i = 0; i < ambient_sensor_count; i++) {
            FILE *f = fopen(ambient_sensors[i].path, "r");
            if (f) {
                int raw = 0;
                if (fscanf(f, "%d", &raw) == 1) {
                    int temp = raw / 1000;
                    if (ambient_sensors[i].cal_idx < 10) {
                        ambient_sensors[i].calibration_readings[ambient_sensors[i].cal_idx++] = temp;
                    }
                }
                fclose(f);
            }
        }
        console_printf(".");
        fflush(stdout);
        sleep(1);
    }
    console_printf(" done.\n");
    
    int trusted_count = 0;
    for (int i = 0; i < ambient_sensor_count; i++) {
        if (ambient_sensors[i].cal_idx < 3) continue;
        
        int sum = 0;
        int min = 999, max = 0;
        for (int j = 0; j < ambient_sensors[i].cal_idx; j++) {
            int t = ambient_sensors[i].calibration_readings[j];
            sum += t;
            if (t < min) min = t;
            if (t > max) max = t;
        }
        int avg = sum / ambient_sensors[i].cal_idx;
        int stability = max - min;
        
        if (avg >= 20 && avg <= 60 && stability <= 5) {
            ambient_sensors[i].valid = 1;
            ambient_sensors[i].trusted = 1;
            trusted_count++;
            console_printf("    ✓ %s: avg=%d°C, stable=%d°C - TRUSTED\n",
                ambient_sensors[i].name, avg, stability);
        } else {
            console_printf("    ✗ %s: avg=%d°C, stable=%d°C - UNRELIABLE\n",
                ambient_sensors[i].name, avg, stability);
        }
    }
    
        console_printf("  Trusted sensors for ambient: %d/%d\n", trusted_count, ambient_sensor_count);
}

static int calculate_ambient_temp(void) {
    int sum = 0;
    int count = 0;
    
    for (int i = 0; i < ambient_sensor_count; i++) {
        if (ambient_sensors[i].trusted) {
            sum += ambient_sensors[i].temp;
            count++;
        }
    }
    
    if (count > 0) {
        int avg = sum / count;
        if (validate_temperature(avg, ambient_last_valid)) {
            ambient_last_valid = avg;
            return avg;
        }
    }
    
    return ambient_last_valid;
}

static void update_ambient_temp(void) {
    for (int i = 0; i < ambient_sensor_count; i++) {
        if (!ambient_sensors[i].trusted) continue;
        
        FILE *f = fopen(ambient_sensors[i].path, "r");
        if (f) {
            int raw = 0;
            if (fscanf(f, "%d", &raw) == 1) {
                ambient_sensors[i].temp = raw / 1000;
            }
            fclose(f);
        }
    }
    
    share_info->ambient_temp = calculate_ambient_temp();
}

//============================================================================
// EC функции
//============================================================================

static int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0 || ioperm(EC_SC, 1, 1) != 0) {
        return 0;
    }
    return 1;
}

static int ec_wait(uint32_t port, uint32_t flag, int value) {
    int i = 0;
    uint8_t data;
    while (i++ < 100) {
        data = inb(port);
        if (((data >> flag) & 1) == value) {
            return 1;
        }
        usleep(1000);
    }
    return 0;
}

static uint8_t ec_read(uint8_t reg) {
    if (!ec_wait(EC_SC, IBF, 0)) return 0;
    outb(EC_SC_READ_CMD, EC_SC);
    if (!ec_wait(EC_SC, IBF, 0)) return 0;
    outb(reg, EC_DATA);
    if (!ec_wait(EC_SC, OBF, 1)) return 0;
    return inb(EC_DATA);
}

static int ec_write(uint8_t cmd, uint8_t port, uint8_t value) {
    if (!ec_wait(EC_SC, IBF, 0)) return 0;
    outb(cmd, EC_SC);
    if (!ec_wait(EC_SC, IBF, 0)) return 0;
    outb(port, EC_DATA);
    if (!ec_wait(EC_SC, IBF, 0)) return 0;
    outb(value, EC_DATA);
    return ec_wait(EC_SC, IBF, 0);
}

static void ec_set_auto_mode(void) {
    ec_write(EC_CMD_FAN, EC_PORT_CPU, AUTO_FAN_VALUE);
    usleep(50000);
    ec_write(EC_CMD_FAN, EC_PORT_GPU, AUTO_FAN_VALUE);
}

static void ec_read_fan_status(void) {
    if (!share_info) return;
    
    int raw = ec_read(EC_REG_FAN_CPU_DUTY);
    share_info->cpu_fan.duty = (raw * 100) / 255;
    
    int hi = ec_read(EC_REG_FAN_CPU_RPMS_HI);
    int lo = ec_read(EC_REG_FAN_CPU_RPMS_LO);
    int rpm_raw = (hi << 8) + lo;
    share_info->cpu_fan.rpms = rpm_raw > 0 ? (2156220 / rpm_raw) : 0;
    
    raw = ec_read(EC_REG_FAN_GPU_DUTY);
    share_info->gpu_fan.duty = (raw * 100) / 255;
    
    hi = ec_read(EC_REG_FAN_GPU_RPMS_HI);
    lo = ec_read(EC_REG_FAN_GPU_RPMS_LO);
    rpm_raw = (hi << 8) + lo;
    share_info->gpu_fan.rpms = rpm_raw > 0 ? (2156220 / rpm_raw) : 0;
}

static int ec_write_cpu_fan(int duty) {
    int val = (duty * 255) / 100;
    return ec_write(EC_CMD_FAN, EC_PORT_CPU, val);
}

static int ec_write_gpu_fan(int duty) {
    int val = (duty * 255) / 100;
    return ec_write(EC_CMD_FAN, EC_PORT_GPU, val);
}

static int ec_write_both_fans(int cpu, int gpu) {
    ec_write_cpu_fan(cpu);
    usleep(50000);
    return ec_write_gpu_fan(gpu);
}

static int ec_read_cpu_temp(void) {
    return ec_read(EC_REG_CPU_TEMP);
}

static int ec_read_gpu_temp(void) {
    return ec_read(EC_REG_GPU_TEMP);
}

//============================================================================
// NVIDIA NVML функции
//============================================================================

static void nvidia_init(void) {
    const char* libs[] = {"libnvidia-ml.so.1", "/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1", NULL};
    
    for (int i = 0; libs[i]; i++) {
        nvidia_handle = dlopen(libs[i], RTLD_LAZY);
        if (nvidia_handle) break;
    }
    
    if (!nvidia_handle) return;
    
    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
    
    auto nvmlInit = (nvmlInit_t)dlsym(nvidia_handle, "nvmlInit_v2");
    auto nvmlGetHandle = (nvmlDeviceGetHandleByIndex_t)dlsym(nvidia_handle, "nvmlDeviceGetHandleByIndex_v2");
    auto nvmlGetTemp = (nvmlDeviceGetTemperature_t)dlsym(nvidia_handle, "nvmlDeviceGetTemperature");
    
    if (!nvmlInit || !nvmlGetHandle || !nvmlGetTemp) {
        dlclose(nvidia_handle);
        nvidia_handle = NULL;
        return;
    }
    
    if (nvmlInit() != 0) {
    dlclose(nvidia_handle);
    nvidia_handle = NULL;
    return;
    }
    
    void *device;
    if (nvmlGetHandle(0, &device) == 0) {
        unsigned int temp;
        if (nvmlGetTemp(device, 0, &temp) == 0) {
            share_info->nvidia_temp = (int)temp;
            share_info->nvidia_available = 1;
            nvidia_nvml_available = 1;
            console_printf("✓ NVIDIA NVML detected: %d°C\n", share_info->nvidia_temp);
        }
    }
}

static void nvidia_update(void) {
    if (!nvidia_nvml_available || !nvidia_handle) return;
    
    typedef int (*nvmlInit_t)(void);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(int, void**);
    typedef int (*nvmlDeviceGetTemperature_t)(void*, int, unsigned int*);
    
    auto nvmlInit = (nvmlInit_t)dlsym(nvidia_handle, "nvmlInit_v2");
    auto nvmlGetHandle = (nvmlDeviceGetHandleByIndex_t)dlsym(nvidia_handle, "nvmlDeviceGetHandleByIndex_v2");
    auto nvmlGetTemp = (nvmlDeviceGetTemperature_t)dlsym(nvidia_handle, "nvmlDeviceGetTemperature");
    
    if (!nvmlInit || !nvmlGetHandle || !nvmlGetTemp) return;
    
    if (nvmlInit() != 0) return;
    
    void *device;
    if (nvmlGetHandle(0, &device) == 0) {
        unsigned int temp;
        if (nvmlGetTemp(device, 0, &temp) == 0) {
            share_info->nvidia_temp = (int)temp;
        }
    }
}

//============================================================================
// NVMe функции
//============================================================================

static char nvme0_path[256] = {0};
static char nvme1_path[256] = {0};

static void scan_nvme_disks(void) {
    console_printf("\n💾 Scanning NVMe drives...\n");
    
    share_info->nvme1_temp = 0;
    share_info->nvme2_temp = 0;
    memset(nvme0_path, 0, sizeof(nvme0_path));
    memset(nvme1_path, 0, sizeof(nvme1_path));
    
    for (int nvme_idx = 0; nvme_idx < 2; nvme_idx++) {
        char base_path[256];
        snprintf(base_path, sizeof(base_path), "/sys/class/nvme/nvme%d", nvme_idx);
        
        struct stat st;
        if (stat(base_path, &st) != 0) {
            continue;
        }
        
        int found_temp = 0;
        char *saved_path = (nvme_idx == 0) ? nvme0_path : nvme1_path;
        
        // Пробуем стандартные пути
        const char* test_paths[] = {
            "/sys/class/nvme/nvme%d/device/temp1_input",
            "/sys/class/nvme/nvme%d/temperature",
            NULL
        };
        
        for (int p = 0; test_paths[p] != NULL; p++) {
            char temp_path[256];
            snprintf(temp_path, sizeof(temp_path), test_paths[p], nvme_idx);
            
            FILE *f = fopen(temp_path, "r");
            if (f) {
                int val;
                if (fscanf(f, "%d", &val) == 1) {
                    int temp = (val > 1000) ? val / 1000 : val;
                    if (temp >= 20 && temp <= 100) {
                        found_temp = 1;
                        strcpy(saved_path, temp_path);
                        
                        if (nvme_idx == 0) {
                            share_info->nvme1_temp = temp;
                        } else {
                            share_info->nvme2_temp = temp;
                        }
                        
                        char model_path[256];
                        char model[128] = "Unknown NVMe";
                        snprintf(model_path, sizeof(model_path), "/sys/class/nvme/nvme%d/device/model", nvme_idx);
                        
                        FILE *mf = fopen(model_path, "r");
                        if (mf) {
                            if (fgets(model, sizeof(model), mf)) {
                                model[strcspn(model, "\n")] = 0;
                                char *p = model;
                                while (*p == ' ') p++;
                                if (p != model) memmove(model, p, strlen(p) + 1);
                                p = model + strlen(model) - 1;
                                while (p > model && *p == ' ') *p-- = 0;
                            }
                            fclose(mf);
                        }
                        console_printf("  Found NVMe%d: %s - %d°C\n", nvme_idx, model, temp);
                        break;
                    }
                }
                fclose(f);
                if (found_temp) break;
            }
        }
        
        // Если не нашли через простые пути, ищем один раз через find
        if (!found_temp) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "find /sys -name \"temp*_input\" -path \"*nvme%d*\" 2>/dev/null | head -1", nvme_idx);
            FILE *fp = popen(cmd, "r");
            if (fp) {
                char temp_path[512];
                if (fgets(temp_path, sizeof(temp_path), fp)) {
                    temp_path[strcspn(temp_path, "\n")] = 0;
                    if (strlen(temp_path) > 0) {
                        FILE *f = fopen(temp_path, "r");
                        if (f) {
                            int val;
                            if (fscanf(f, "%d", &val) == 1) {
                                int temp = (val > 1000) ? val / 1000 : val;
                                if (temp >= 20 && temp <= 100) {
                                    strcpy(saved_path, temp_path);
                                    if (nvme_idx == 0) {
                                        share_info->nvme1_temp = temp;
                                    } else {
                                        share_info->nvme2_temp = temp;
                                    }
                                    console_printf("  Found NVMe%d: %d°C\n", nvme_idx, temp);
                                }
                            }
                            fclose(f);
                        }
                    }
                }
                pclose(fp);
            }
        }
    }
    
    if (share_info->nvme1_temp == 0 && share_info->nvme2_temp == 0) {
        console_printf("  No NVMe drives found\n");
    } else {
        console_printf("  Found %d NVMe device(s)\n", 
               (share_info->nvme1_temp > 0 ? 1 : 0) + (share_info->nvme2_temp > 0 ? 1 : 0));
    }
}

static void update_nvme_temps(void) {
    // Обновляем NVMe0 - прямое чтение без popen
    if (share_info->nvme1_temp > 0 && nvme0_path[0] != '\0') {
        FILE *f = fopen(nvme0_path, "r");
        if (f) {
            int val;
            if (fscanf(f, "%d", &val) == 1) {
                int temp = (val > 1000) ? val / 1000 : val;
                if (temp >= 20 && temp <= 100) {
                    share_info->nvme1_temp = temp;
                }
            }
            fclose(f);
        }
    }
    
    // Обновляем NVMe1 - прямое чтение без popen
    if (share_info->nvme2_temp > 0 && nvme1_path[0] != '\0') {
        FILE *f = fopen(nvme1_path, "r");
        if (f) {
            int val;
            if (fscanf(f, "%d", &val) == 1) {
                int temp = (val > 1000) ? val / 1000 : val;
                if (temp >= 20 && temp <= 100) {
                    share_info->nvme2_temp = temp;
                }
            }
            fclose(f);
        }
    }
}

//============================================================================
// Логирование
//============================================================================

static void log_to_journal(const char* level, const char* format, ...) {
    pthread_mutex_lock(&log_mutex);
    va_list args;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    char buffer[1024];
    
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Всегда пишем в stdout (идёт в journal для службы)
    printf("[%s] %s: %s\n", time_str, level, buffer);
    fflush(stdout);
    
    // Если включено файловое логирование - пишем в файл
    if (file_logging_enabled && log_file) {
        fprintf(log_file, "[%s] %s: %s\n", time_str, level, buffer);
        fflush(log_file);
    };
    pthread_mutex_unlock(&log_mutex);
}

static void init_log_files(void) {
    if (!file_logging_enabled) {
        console_printf("✓ Logging to systemd journal only\n");
        console_printf("  View logs: sudo journalctl -u clevo-fan-control -f\n");
        return;
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[512];
    
    snprintf(filename, sizeof(filename), "%s/clevo_fan_control_%04d%02d%02d_%02d%02d%02d.log",
             log_dir, tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    log_file = fopen(filename, "w");
    if (log_file) {
        fprintf(log_file, "Clevo Fan Control Daemon v%s\n", VERSION);
        fprintf(log_file, "========================================\n");
        fprintf(log_file, "Start time: %s", ctime(&now));
        fprintf(log_file, "Config: %s/fan_curve.conf\n", config_path);
        fprintf(log_file, "GPU temperature source: %s\n\n", 
                share_info->nvidia_available ? "NVML (NVIDIA driver)" : "EC register 0xFB");
        fprintf(log_file, "Format: Timestamp | CPU | GPU(primary) | GPU(EC) | AMBIENT | NVMe1/NVMe2 | CPU_FAN(RPM/%%) | GPU_FAN(RPM/%%)\n\n");
        fflush(log_file);
        console_printf("📝 File logging enabled: %s\n", filename);
        console_printf("   Logs also available via: sudo journalctl -u clevo-fan-control -f\n");
    }
    
    snprintf(filename, sizeof(filename), "%s/fan_test_%04d%02d%02d_%02d%02d%02d.log",
             test_log_dir, tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    test_log = fopen(filename, "w");
    if (test_log) {
        fprintf(test_log, "Fan Diagnostic Test Log\n");
        fprintf(test_log, "=======================\n");
        fprintf(test_log, "Start time: %s", ctime(&now));
        fprintf(test_log, "Test duration per step: %d seconds\n\n", TEST_STEP_DURATION);
        fflush(test_log);
        console_printf("📝 Test log file: %s\n", filename);
    }
}

static void log_sensor_data(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    int gpu_primary = share_info->nvidia_available ? share_info->nvidia_temp : share_info->gpu_temp_filtered;
    
    char log_line[1024];
    snprintf(log_line, sizeof(log_line),
            "CPU:%d | GPU:%d | GPU_EC:%d | AMB:%d | NVMe:%d/%d | FAN_CPU:%d/%d%% | FAN_GPU:%d/%d%% | %s",
            share_info->cpu_temp_filtered,
            gpu_primary,
            share_info->gpu_temp_raw,
            share_info->ambient_temp,
            share_info->nvme1_temp,
            share_info->nvme2_temp,
            share_info->cpu_fan.rpms,
            share_info->cpu_fan.duty,
            share_info->gpu_fan.rpms,
            share_info->gpu_fan.duty,
            share_info->reason);
    
    // Логирование в systemd journal (с интервалом из конфига)
    static time_t last_journal_log = 0;
    if (last_journal_log == 0 || difftime(now, last_journal_log) >= config.journal_log_interval_sec) {
        log_to_journal("INFO", "%s", log_line);
        last_journal_log = now;
    }
    
    // Файловое логирование (всегда, если включено)
    if (file_logging_enabled && log_file) {
        fprintf(log_file, "[%02d:%02d:%02d] %s\n",
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, log_line);
        fflush(log_file);
    }
}

static void log_test_result(const char* test_name, int cpu_rpm, int gpu_rpm, int cpu_duty, int gpu_duty) {
    if (!test_log) return;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char log_line[512];
    
    snprintf(log_line, sizeof(log_line), "%-45s CPU: %4d RPM (%2d%%) | GPU: %4d RPM (%2d%%)",
             test_name, cpu_rpm, cpu_duty, gpu_rpm, gpu_duty);
    
    fprintf(test_log, "[%02d:%02d:%02d] %s\n",
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, log_line);
    fflush(test_log);
    
    // Также пишем в journal
    log_to_journal("TEST", "%s", log_line);
}

//============================================================================
// Управление вентиляторами
//============================================================================

static int get_fan_speed_from_curve(struct FanCurve *curve, int temp) {
    if (curve->num_points == 0) return 0;
    
    for (int i = 0; i < curve->num_points; i++) {
        if (temp <= curve->points[i].temp) {
            return curve->points[i].speed;
        }
    }
    
    return curve->points[curve->num_points - 1].speed;
}

static void smart_control_update(void) {
    static int last_cpu_speed = 0;
    static int last_gpu_speed = 0;
    
    int gpu_temp;
    if (share_info->nvidia_available) {
        gpu_temp = share_info->nvidia_temp;
    } else {
        gpu_temp = share_info->gpu_temp_filtered;
    }
    
    int cpu_temp = share_info->cpu_temp_filtered;
    int nvme1_temp = share_info->nvme1_temp;
    int nvme2_temp = share_info->nvme2_temp;
    int max_nvme_temp = (nvme1_temp > nvme2_temp) ? nvme1_temp : nvme2_temp;
    
    // Аварийный режим NVMe - приоритет выше CPU/GPU
    if (max_nvme_temp >= NVME_CRITICAL_TEMP) {
        share_info->cpu_target = 100;
        share_info->gpu_target = 100;
        share_info->emergency = 1;
        last_cpu_speed = 100;
        last_gpu_speed = 100;
        snprintf(share_info->reason, sizeof(share_info->reason),
                "⚠️ NVMe CRITICAL: %d°C → FULL SPEED (100%%)", max_nvme_temp);
        return;
    }
    
    if (max_nvme_temp >= NVME_WARNING_TEMP) {
        share_info->cpu_target = 90;
        share_info->gpu_target = 90;
        share_info->emergency = 1;
        last_cpu_speed = 90;
        last_gpu_speed = 90;
        snprintf(share_info->reason, sizeof(share_info->reason),
                "⚠️ NVMe WARNING: %d°C → HIGH SPEED (90%%)", max_nvme_temp);
        return;
    }
    
    // Аварийная проверка CPU/GPU
    if (cpu_temp >= config.emergency_temp || gpu_temp >= config.emergency_temp) {
        share_info->cpu_target = 100;
        share_info->gpu_target = 100;
        share_info->emergency = 1;
        last_cpu_speed = 100;
        last_gpu_speed = 100;
        snprintf(share_info->reason, sizeof(share_info->reason),
                "⚠️ EMERGENCY: CPU=%d°C GPU=%d°C → FULL SPEED", cpu_temp, gpu_temp);
        return;
    }
    
    // Нормальный режим
    share_info->emergency = 0;
    
    int cpu_speed = get_fan_speed_from_curve(&config.cpu_curve, cpu_temp);
    int gpu_speed = get_fan_speed_from_curve(&config.gpu_curve, gpu_temp);
    
    // Дополнительный буст от NVMe при нормальной работе
    if (max_nvme_temp > 55 && max_nvme_temp < NVME_WARNING_TEMP) {
        int nvme_boost = (max_nvme_temp - 55) * 2;
        if (nvme_boost > 20) nvme_boost = 20;
        cpu_speed += nvme_boost;
        gpu_speed += nvme_boost;
        if (cpu_speed > 100) cpu_speed = 100;
        if (gpu_speed > 100) gpu_speed = 100;
    }
    
    // Плавное изменение скорости
    if (abs(cpu_speed - last_cpu_speed) > 10) {
        cpu_speed = last_cpu_speed + (cpu_speed > last_cpu_speed ? 10 : -10);
    }
    if (abs(gpu_speed - last_gpu_speed) > 10) {
        gpu_speed = last_gpu_speed + (gpu_speed > last_gpu_speed ? 10 : -10);
    }
    
    last_cpu_speed = cpu_speed;
    last_gpu_speed = gpu_speed;
    
    share_info->cpu_target = cpu_speed;
    share_info->gpu_target = gpu_speed;
    
    // Формируем информативное сообщение
    if (max_nvme_temp > 55) {
        snprintf(share_info->reason, sizeof(share_info->reason),
                "CPU:%d°C→%d%% GPU:%d°C→%d%% NVMe:%d°C (+%d%% boost)",
                cpu_temp, cpu_speed, gpu_temp, gpu_speed, max_nvme_temp, 
                (cpu_speed - get_fan_speed_from_curve(&config.cpu_curve, cpu_temp)));
    } else {
        snprintf(share_info->reason, sizeof(share_info->reason),
                "CPU:%d°C→%d%% GPU:%d°C→%d%%",
                cpu_temp, cpu_speed, gpu_temp, gpu_speed);
    }
}

//============================================================================
// Диагностические тесты
//============================================================================

static void test_fan_pair(int cpu_speed, int gpu_speed, const char* description) {
    console_printf("  %s... ", description);
    fflush(stdout);
    
    ec_write_both_fans(cpu_speed, gpu_speed);
    sleep(TEST_STEP_DURATION);
    
    ec_read_fan_status();
    log_test_result(description, share_info->cpu_fan.rpms, share_info->gpu_fan.rpms,
                    share_info->cpu_fan.duty, share_info->gpu_fan.duty);
    
    console_printf("CPU: %4d RPM (%2d%%), GPU: %4d RPM (%2d%%)\n",
           share_info->cpu_fan.rpms, share_info->cpu_fan.duty,
           share_info->gpu_fan.rpms, share_info->gpu_fan.duty);
}

static void test_fan_pair_phase(int cpu_start, int gpu_start, int cpu_end, int gpu_end, const char* description) {
    console_printf("\n  %s\n", description);
    console_printf("  ┌─────────────────────────────────────────────────────────────────┐\n");
    
    int cpu_step = (cpu_end - cpu_start) / 10;
    int gpu_step = (gpu_end - gpu_start) / 10;
    
    for (int i = 0; i <= 10; i++) {
        int cpu_speed = cpu_start + cpu_step * i;
        int gpu_speed = gpu_start + gpu_step * i;
        
        if (cpu_speed < 0) cpu_speed = 0;
        if (cpu_speed > 100) cpu_speed = 100;
        if (gpu_speed < 0) gpu_speed = 0;
        if (gpu_speed > 100) gpu_speed = 100;
        
        char step_desc[64];
        snprintf(step_desc, sizeof(step_desc), "  Шаг %d/%d: CPU=%d%% GPU=%d%%", 
                 i, 10, cpu_speed, gpu_speed);
        
        console_printf("%s... ", step_desc);
        fflush(stdout);
        
        ec_write_both_fans(cpu_speed, gpu_speed);
        sleep(TEST_STEP_DURATION);
        
        ec_read_fan_status();
        log_test_result(step_desc, share_info->cpu_fan.rpms, share_info->gpu_fan.rpms,
                        share_info->cpu_fan.duty, share_info->gpu_fan.duty);
        
        console_printf("CPU: %4d RPM (%2d%%), GPU: %4d RPM (%2d%%)\n",
               share_info->cpu_fan.rpms, share_info->cpu_fan.duty,
               share_info->gpu_fan.rpms, share_info->gpu_fan.duty);
    }
    console_printf("  └─────────────────────────────────────────────────────────────────┘\n");
}

static void test_single_fan(int fan_type, int speed, const char* description) {
    console_printf("  %s... ", description);
    fflush(stdout);
    
    if (fan_type == 1) {
        ec_write_cpu_fan(speed);
        ec_write_gpu_fan(AUTO_FAN_VALUE);
    } else {
        ec_write_gpu_fan(speed);
        ec_write_cpu_fan(AUTO_FAN_VALUE);
    }
    sleep(TEST_STEP_DURATION);
    
    ec_read_fan_status();
    log_test_result(description, share_info->cpu_fan.rpms, share_info->gpu_fan.rpms,
                    share_info->cpu_fan.duty, share_info->gpu_fan.duty);
    
    console_printf("CPU: %4d RPM (%2d%%), GPU: %4d RPM (%2d%%)\n",
           share_info->cpu_fan.rpms, share_info->cpu_fan.duty,
           share_info->gpu_fan.rpms, share_info->gpu_fan.duty);
}

static void run_diagnostic_test(void) {
    console_printf("\n");
    console_printf("╔══════════════════════════════════════════════════════════════════╗\n");
    console_printf("║                    FAN DIAGNOSTIC TEST                          ║\n");
    console_printf("╚══════════════════════════════════════════════════════════════════╝\n\n");
    
    test_fan_pair_phase(0, 100, 100, 0, "Фаза 1A: Противофазный тест (CPU↑ GPU↓)");
    test_fan_pair_phase(100, 0, 0, 100, "Фаза 1B: Противофазный тест (CPU↓ GPU↑)");
    test_fan_pair_phase(0, 0, 100, 100, "Фаза 2: Синхронный тест (CPU↑ GPU↑)");
    test_fan_pair_phase(100, 100, 0, 0, "Фаза 3: Синхронный тест (CPU↓ GPU↓)");
    
    console_printf("\nФаза 4: Пошаговый тест CPU вентилятора (GPU в AUTO режиме)\n");
    console_printf("────────────────────────────────────────────────────────────────────\n");
    
    int speeds[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    for (int i = 0; i < 11; i++) {
        char desc[64];
        snprintf(desc, sizeof(desc), "CPU %d%% (GPU AUTO)", speeds[i]);
        test_single_fan(1, speeds[i], desc);
    }
    
    console_printf("\nФаза 5: Пошаговый тест GPU вентилятора (CPU в AUTO режиме)\n");
    console_printf("────────────────────────────────────────────────────────────────────\n");
    
    for (int i = 0; i < 11; i++) {
        char desc[64];
        snprintf(desc, sizeof(desc), "GPU %d%% (CPU AUTO)", speeds[i]);
        test_single_fan(2, speeds[i], desc);
    }
    
    console_printf("\nФаза 6: Тест на конфликты - быстрые переключения\n");
    console_printf("────────────────────────────────────────────────────────────────────\n");
    
    int test_speeds[] = {0, 50, 100, 50, 0, 100, 0, 50, 100};
    for (int i = 0; i < 9; i++) {
        char desc[64];
        snprintf(desc, sizeof(desc), "Быстрый тест: CPU=%d%% GPU=%d%%", 
                 test_speeds[i], 100 - test_speeds[i]);
        test_fan_pair(test_speeds[i], 100 - test_speeds[i], desc);
    }
    
    console_printf("\n🔄 Завершение теста: возврат в AUTO режим...\n");
    ec_set_auto_mode();
    sleep(2);
    
    ec_read_fan_status();
    console_printf("\n✅ Диагностический тест завершен!\n");
    console_printf("   Результаты сохранены в %s/\n", test_log_dir);
}

//============================================================================
// Вывод статуса
//============================================================================

static void print_status(void) {
    if (!interactive_mode) return;  // В режиме службы не выводим статус в консоль
    
    console_printf("\n╔════════════════════════════════════════════════════════════╗\n");
    console_printf("║           Clevo Fan Control v%s                      ║\n", VERSION);
    console_printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    console_printf("GPU Temperature Source: %s\n", 
           share_info->nvidia_available ? "NVML (NVIDIA driver)" : "EC register 0xFB");
    console_printf("Config: %s/fan_curve.conf\n", config_path);
    
    console_printf("\nCurrent Status:\n");
    console_printf("  CPU: %d°C → Fan: %d RPM (%d%%)\n", 
           share_info->cpu_temp_filtered,
           share_info->cpu_fan.rpms,
           share_info->cpu_fan.duty);
    
    int gpu_display = share_info->nvidia_available ? share_info->nvidia_temp : share_info->gpu_temp_filtered;
    console_printf("  GPU: %d°C → Fan: %d RPM (%d%%)\n",
           gpu_display,
           share_info->gpu_fan.rpms,
           share_info->gpu_fan.duty);
    
    int max_nvme = (share_info->nvme1_temp > share_info->nvme2_temp) ? 
                   share_info->nvme1_temp : share_info->nvme2_temp;
    const char* nvme_warning = "";
    if (max_nvme >= NVME_CRITICAL_TEMP) {
        nvme_warning = " ⚠️ CRITICAL!";
    } else if (max_nvme >= NVME_WARNING_TEMP) {
        nvme_warning = " ⚠️ WARNING!";
    }
    
    console_printf("  NVMe: %d°C / %d°C%s\n", 
           share_info->nvme1_temp, 
           share_info->nvme2_temp,
           nvme_warning);
    console_printf("  Ambient: %d°C\n", share_info->ambient_temp);
    console_printf("  Mode: %s\n", share_info->smart_mode ? "SMART" : "PASSIVE");
    console_printf("  Target: CPU=%d%% GPU=%d%%\n", share_info->cpu_target, share_info->gpu_target);
    console_printf("  %s\n", share_info->reason);
}

//============================================================================
// Worker thread
//============================================================================

static void* worker_thread_func(void*) {
    console_printf("Worker thread started\n");
    
    int last_cpu_target = -1;
    int last_gpu_target = -1;
    static int last_valid_cpu = 40;
    static int last_valid_gpu = 40;
    
    while (!share_info->exit) {
        if (!share_info->diagnostic_mode) {
            int cpu_raw = ec_read_cpu_temp();
            int gpu_raw = ec_read_gpu_temp();
            
            share_info->cpu_temp_raw = cpu_raw;
            share_info->gpu_temp_raw = gpu_raw;
            
            share_info->cpu_temp_filtered = filter_temperature(cpu_raw, &cpu_filter, &cpu_history, &last_valid_cpu);
            share_info->gpu_temp_filtered = filter_temperature(gpu_raw, &gpu_filter, &gpu_history, &last_valid_gpu);
            
            update_ambient_temp();
            update_nvme_temps();
            
            if (share_info->nvidia_available) {
                nvidia_update();
            }
            
            ec_read_fan_status();
            log_sensor_data();
            
            if (share_info->smart_mode) {
                smart_control_update();
                
                int cpu = share_info->cpu_target;
                int gpu = share_info->gpu_target;
                
                if (cpu != last_cpu_target || gpu != last_gpu_target) {
                    if (share_info->sync_fans) {
                        int target = (cpu + gpu) / 2;
                        ec_write_both_fans(target, target);
                    } else {
                        ec_write_cpu_fan(cpu);
                        ec_write_gpu_fan(gpu);
                    }
                    last_cpu_target = cpu;
                    last_gpu_target = gpu;
                }
            }
        } else {
            ec_read_fan_status();
        }
        
        usleep(config.update_interval_ms * 1000);
    }
    
    console_printf("Worker thread stopped\n");
    return NULL;
}

//============================================================================
// Signal handler
//============================================================================

static void signal_handler(int sig) {
    console_printf("\nShutting down...\n");
    
    if (share_info) {
        share_info->exit = 1;
        ec_set_auto_mode();
    }
    
    // Закрываем NVML handle, если он открыт
    if (nvidia_handle) {
        typedef int (*nvmlShutdown_t)(void);
        nvmlShutdown_t nvmlShutdown = (nvmlShutdown_t)dlsym(nvidia_handle, "nvmlShutdown");
        
        if (nvmlShutdown) {
            nvmlShutdown();
        }
        
        dlclose(nvidia_handle);
        nvidia_handle = NULL;
        nvidia_nvml_available = 0;
    }
}

//============================================================================
// Main
//============================================================================

int main(int argc, char *argv[]) {
    int status_mode = 0;
    int stop_daemon = 0;
    int run_test = 0;
    int log_mode = 0;
    int help_mode = 0;

    if (getenv("INVOCATION_ID") != NULL) {
        interactive_mode = 0;  // Запущено как служба
    } else {
        interactive_mode = 1;  // Интерактивный режим
    }
      
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "status") == 0) {
            status_mode = 1;
        } else if (strcmp(argv[i], "stop") == 0) {
            stop_daemon = 1;
        } else if (strcmp(argv[i], "test") == 0) {
            run_test = 1;
        } else if (strcmp(argv[i], "log") == 0) {
            log_mode = 1;
        } else if (strcmp(argv[i], "help") == 0 || strcmp(argv[i], "-h") == 0 || 
                   strcmp(argv[i], "--help") == 0) {
            help_mode = 1;
        }
    }
    
    get_paths(argc, argv);
    init_default_config();
    load_config();
    
    if (help_mode) {
        print_help();
        return 0;
    }
    
    void *shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    share_info = (SharedInfo*)shm;
    memset(share_info, 0, sizeof(SharedInfo));
    share_info->smart_mode = 1;
    share_info->cpu_temp_filtered = 40;
    share_info->gpu_temp_filtered = 40;
    share_info->diagnostic_mode = 1;
    share_info->ambient_temp = 35;
    
    if (!ec_init()) {
        console_printf("❌ Cannot access EC ports. Run with sudo.\n");
        return 1;
    }
    
    nvidia_init();
    scan_ambient_sensors();
    
    if (status_mode) {
        share_info->cpu_temp_raw = ec_read_cpu_temp();
        share_info->gpu_temp_raw = ec_read_gpu_temp();
        update_ambient_temp();
        update_nvme_temps();
        ec_read_fan_status();
        
        console_printf("\n╔════════════════════════════════════════════════════════════╗\n");
        console_printf("║           Clevo Fan Control v%s                      ║\n", VERSION);
        console_printf("╚════════════════════════════════════════════════════════════╝\n\n");
        
        console_printf("Config: %s/fan_curve.conf\n", config_path);
        console_printf("Logs: %s/\n", log_dir);
        console_printf("GPU Temperature Source: %s\n", 
            share_info->nvidia_available ? "NVML (NVIDIA driver)" : "EC register 0xFB");
        console_printf("\nCurrent Status:\n");
        console_printf("  CPU: %d°C → Fan: %d RPM (%d%%)\n", 
            share_info->cpu_temp_raw,
            share_info->cpu_fan.rpms,
            share_info->cpu_fan.duty);
        
        int gpu_display = share_info->nvidia_available ? share_info->nvidia_temp : share_info->gpu_temp_raw;
        console_printf("  GPU: %d°C → Fan: %d RPM (%d%%)\n",
            gpu_display,
            share_info->gpu_fan.rpms,
            share_info->gpu_fan.duty);
        
        console_printf("  NVMe: %d°C / %d°C\n", share_info->nvme1_temp, share_info->nvme2_temp);
        console_printf("  Ambient: %d°C\n", share_info->ambient_temp);
        console_printf("  Mode: %s\n", share_info->smart_mode ? "SMART" : "PASSIVE");
        
        return 0;
    }
    
    if (stop_daemon) {
        console_printf("Stopping daemon...\n");  // ← заменить printf на console_printf
        share_info->exit = 1;
        ec_set_auto_mode();
        return 0;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGCHLD, SIG_IGN);
    
    init_median_filter(&cpu_filter);
    init_median_filter(&gpu_filter);
    init_history(&cpu_history);
    init_history(&gpu_history);
    
    scan_nvme_disks();
    init_log_files();
    
    pthread_create(&worker_thread, NULL, worker_thread_func, NULL);
    
    console_printf("\n╔════════════════════════════════════════════════════════════╗\n");
    console_printf("║           Clevo Fan Control v%s                      ║\n", VERSION);
    console_printf("║           Intelligent fan control daemon                  ║\n");
    console_printf("╚════════════════════════════════════════════════════════════╝\n\n");

    console_printf("✓ EC I/O permissions granted\n");
    console_printf("✓ %s\n", share_info->nvidia_available ? "NVIDIA NVML detected" : "Using EC register 0xFB for GPU temp");
    console_printf("✓ Ambient sensors: %d trusted\n", ambient_sensor_count);
    console_printf("✓ Config loaded from: %s/fan_curve.conf\n", config_path);
    console_printf("✓ Log directory: %s/\n", log_dir);
    console_printf("✓ Filtering: Median window=%d, History=%d samples\n", config.median_window, config.history_size);
    console_printf("✓ NVMe protection: %d°C warning, %d°C critical\n", NVME_WARNING_TEMP, NVME_CRITICAL_TEMP);

    if (run_test) {
        run_diagnostic_test();
    } else {
        console_printf("\nℹ️  Starting without diagnostic test (use 'test' option to run test)\n");
        ec_set_auto_mode();
        sleep(1);
    }

    share_info->diagnostic_mode = 0;
    share_info->smart_mode = 1;
    ec_set_auto_mode();
    sleep(1);

    console_printf("\n");
    console_printf("╔════════════════════════════════════════════════════════════╗\n");
    console_printf("║           NORMAL OPERATION MODE                           ║\n");
    console_printf("║           Intelligent fan control active                  ║\n");
    console_printf("╚════════════════════════════════════════════════════════════╝\n\n");

    if (log_mode) {
        file_logging_enabled = 1;
        console_printf("📁 FILE LOGGING MODE ACTIVE\n");
        console_printf("   Logs are written to: %s/clevo_fan_control_*.log\n", log_dir);
        console_printf("   Logs are also available via: sudo journalctl -u clevo-fan-control -f\n\n");
    } else {
        console_printf("📝 Logging to systemd journal only\n");
        console_printf("   View logs: sudo journalctl -u clevo-fan-control -f\n\n");
        if (interactive_mode) {
            console_printf("Commands:\n");
            console_printf("  %s status     - Show current status\n", argv[0]);
            console_printf("  %s stop       - Stop daemon\n", argv[0]);
            console_printf("  %s test       - Run diagnostic test, then start\n", argv[0]);
            console_printf("  %s log        - Start daemon with file logging\n", argv[0]);
            console_printf("  %s help       - Show help\n", argv[0]);
            console_printf("\nPress Ctrl+C to stop daemon\n\n");
        }
    }

    print_status();

        // ========== ГЛАВНЫЙ ЦИКЛ ОЖИДАНИЯ ==========
    if (interactive_mode) {
        // В интерактивном режиме показываем статус каждую минуту
        while (!share_info->exit) {
            sleep(1);
            static int counter = 0;
            if (++counter >= 60 && !log_mode) {
                counter = 0;
                print_status();
            }
        }
    } else {
        // В режиме службы просто ждём сигнал
        while (!share_info->exit) {
            sleep(1);
        }
    }
    
    // Ожидание завершения рабочего потока
    pthread_join(worker_thread, NULL);
    ec_set_auto_mode();
    
    // Закрытие лог-файлов
    if (log_file) {
        time_t now = time(NULL);
        fprintf(log_file, "\nDaemon stopped: %s", ctime(&now));
        fclose(log_file);
    }
    if (test_log) {
        fclose(test_log);
    }
    
    console_printf("Daemon stopped\n");

    return 0;
}