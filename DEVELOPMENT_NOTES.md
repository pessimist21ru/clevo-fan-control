# Development Notes for Clevo Fan Control Utility

## Version 1.0.2 (Current Release)

## Key Findings

### 1. GPU Temperature Register
**CORRECT:** `0xFB`
**INCORRECT:** `0xCD` (always returns 0)

✅ **Fixed in current version** - Using `0xFB` register.

### 2. Fan Control Method
Both CPU and GPU fans are controlled via EC command `0x99` with specific ports:
- CPU: `ec_write(0x99, 0x01, duty_value)`
- GPU: `ec_write(0x99, 0x02, duty_value)`

✅ **Implemented** - Direct fan control via EC commands.

### 3. Module Conflicts
When `tuxedo_io` or `clevo_acpi` are loaded, direct ioperm access still works. 
⚠️ No need to unload modules - the utility works with them loaded.

### 4. NVMe Temperature Detection
NVMe temperature files can be located at:
- `/sys/class/nvme/nvme0/device/temp1_input`
- `/sys/class/nvme/nvme0/temperature`
- `/sys/devices/pci*/nvme/nvme0/hwmon*/temp1_input`

✅ **Fixed in current version** - Automatic path detection at startup, paths cached for performance.

## Verified EC Registers

| Register | Name | Access | Value Range | Status |
|----------|------|--------|-------------|--------|
| 0x07 | CPU_TEMP | RO | 20-100°C | ✅ Verified |
| 0xCA | AMBIENT_TEMP | RO | 20-60°C | ⚠️ Not used (uses system sensors instead) |
| 0xCE | CPU_FAN_DUTY | RW | 0-255 (0-100%) | ✅ Verified |
| 0xCF | GPU_FAN_DUTY | RW | 0-255 (0-100%) | ✅ Verified |
| 0xD0 | CPU_FAN_RPM_HI | RO | 0-255 | ✅ Verified |
| 0xD1 | CPU_FAN_RPM_LO | RO | 0-255 | ✅ Verified |
| 0xD2 | GPU_FAN_RPM_HI | RO | 0-255 | ✅ Verified |
| 0xD3 | GPU_FAN_RPM_LO | RO | 0-255 | ✅ Verified |
| 0xFB | GPU_TEMP | RO | 20-100°C | ✅ **Correct register** |

## Verified EC Commands

| Command | Port | Value | Effect | Status |
|---------|------|-------|--------|--------|
| 0x99 | 0x01 | 0x00-0xFF | Set CPU fan duty cycle | ✅ Working |
| 0x99 | 0x02 | 0x00-0xFF | Set GPU fan duty cycle | ✅ Working |
| 0x99 | 0xFF | - | Return to AUTO mode | ✅ Working |
| 0x80 | - | - | Read EC register | ✅ Working |

## RPM Calculation

```
RPM = 2156220 / ((high << 8) + low)
```

### Tested Values (GPU Fan)
| Duty (hex) | Duty (%) | RPM | Status |
|------------|----------|-----|--------|
| 0x10 | 6% | 338 | ✅ Verified |
| 0x40 | 25% | 1395 | ✅ Verified |
| 0x80 | 50% | 2623 | ✅ Verified |
| 0xC0 | 75% | 3648 | ✅ Verified |
| 0xFF | 100% | 4577 | ✅ Verified |

## Current Implementation Details

### Temperature Sources

| Component | Source | Priority |
|-----------|--------|----------|
| CPU | EC register 0x07 | Direct |
| GPU | NVML (NVIDIA driver) → EC 0xFB | NVML preferred |
| NVMe | Auto-detected sysfs files | Direct read |
| Ambient | Average of trusted system sensors | Calculated |

### Filtering Parameters

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Median window | 5 samples | Remove single spikes |
| History size | 20 samples (10 seconds) | Detect stable values |
| Valid range | 20-100°C | Filter invalid readings |
| Max change | 30°C per reading | Reject sudden jumps |

### Fan Control Logic

```cpp
// Priority order (highest to lowest)
1. NVMe ≥ 64°C → Fans 100% (critical)
2. NVMe ≥ 60°C → Fans 90% (warning)
3. CPU/GPU ≥ 85°C → Fans 100% (emergency)
4. Normal mode → Fan curve based on temperature
5. NVMe 55-60°C → Additional +20% boost
```

### Default Fan Curves

#### CPU Curve (Max 100%)
| Temperature | Speed |
|-------------|-------|
| <35°C | 0% |
| 40-42°C | 5% |
| 42-45°C | 15% |
| 45-47°C | 26% |
| 47-50°C | 36% |
| 50-55°C | 47% |
| 55-57°C | 58% |
| 57-60°C | 68% |
| 60-62°C | 80% |
| 62-65°C | 90% |
| >65°C | 100% |

#### GPU Curve (Max 90%)
| Temperature | Speed |
|-------------|-------|
| <35°C | 0% |
| 40-42°C | 5% |
| 42-45°C | 10% |
| 45-47°C | 20% |
| 47-50°C | 30% |
| 50-55°C | 40% |
| 55-57°C | 50% |
| 57-60°C | 60% |
| 60-62°C | 70% |
| 62-65°C | 80% |
| >65°C | 100% |

## Directory Structure

```c
// XDG Base Directory Specification
config_path = "~/.config/clevo-fan-control"
log_dir     = "~/.local/share/clevo-fan-control/logs"
state_dir   = "~/.local/state/clevo-fan-control"
test_log_dir = "~/.local/share/clevo-fan-control/logs"

// Файлы
fan_curve.conf          = "~/.config/clevo-fan-control/fan_curve.conf"
clevo_fan_control_*.log = "~/.local/share/clevo-fan-control/logs/clevo_fan_control_*.log"
fan_test_*.log          = "~/.local/share/clevo-fan-control/logs/fan_test_*.log"

// FHS (Filesystem Hierarchy Standard)
config_path = "/etc/clevo-fan-control"
log_dir     = "/var/log/clevo-fan-control"
state_dir   = "/var/lib/clevo-fan-control"
test_log_dir = "/var/log/clevo-fan-control"

// Файлы
fan_curve.conf          = "/etc/clevo-fan-control/fan_curve.conf"
clevo_fan_control_*.log = "/var/log/clevo-fan-control/clevo_fan_control_*.log"
fan_test_*.log          = "/var/log/clevo-fan-control/fan_test_*.log"
```

## Testing Commands

```bash
# Normal operation
sudo clevo-fan-control

# With diagnostic test
sudo clevo-fan-control test

# With continuous logging
sudo clevo-fan-control log

# Show status
sudo clevo-fan-control status

# Stop daemon
sudo clevo-fan-control stop

# Show help
sudo clevo-fan-control help
```

## Diagnostic Utilities (in utilites/)

| Utility | Purpose |
|---------|---------|
| `ec_gpu_temp_scanner.cpp` | Scan EC registers for GPU temperature |
| `thermal_scanner.cpp` | Scan system thermal sensors |
| `nvme_diag.cpp` | Diagnose NVMe temperature paths |
| `gpu_fan_test.cpp` | Test GPU fan control |

## Performance Optimizations

1. **NVMe paths cached** - Found once at startup, not searched in loop
2. **No popen() in worker thread** - Eliminates USB audio latency
3. **Direct file reads** - Minimal overhead for temperature updates
4. **Median filter** - Efficient O(n log n) sorting of small window

## Known Issues & Solutions

| Issue | Solution |
|-------|----------|
| GPU temp shows 0°C | Use register 0xFB instead of 0xCD |
| USB audio stuttering | Cache NVMe paths, avoid popen() in loop |
| Module conflicts | Work without unloading modules |
| NVMe not detected | Automatic path detection at startup |

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.2 | 2026-03-31 | NVMe protection, USB audio fix, path caching |
| 1.0.1 | 2026-03-31 | Fixed NVMe detection, XDG paths |
| 1.0.0 | 2026-03-31 | Initial stable release |
| 0.9.x | 2026-03-29-30 | Development versions |

## Credits

- Original project: [Agramian/clevo-fan-control](https://github.com/agramian/clevo-fan-control)
- Development assistance: DeepSeek AI
- Testing: Clevo N960KPx / Hasee TX9-CA5DP, Linux Debian 13 (Trixie)