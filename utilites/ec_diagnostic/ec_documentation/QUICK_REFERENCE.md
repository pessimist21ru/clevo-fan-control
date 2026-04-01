# EC Documentation for Clevo N960KPx (Hasee TX9-CA5DP)

## Hardware Information
| Parameter | Value |
|-----------|-------|
| Model | Notebook N960Kx (Clevo N960KPx) |
| BIOS | INSYDE Corp. 1.07.14 (05/18/2022) |
| Chipset | Intel H570 |
| EC Controller | Integrated in ITE Super I/O (LPC) |
| GPU | NVIDIA GeForce RTX 3070 Laptop GPU |

## EC Access Methods
| Method | Interface | Status |
|--------|-----------|--------|
| ioperm (direct I/O) | Ports 0x62, 0x66 | ✅ Working |
| ec_sys debugfs | /sys/kernel/debug/ec/ec0/io | ✅ Working |
| tuxedo_io sysfs | /sys/devices/platform/tuxedo_io/ | ✅ Loaded |
| clevo_acpi sysfs | /sys/devices/platform/clevo_acpi/ | ✅ Loaded |

## Verified EC Registers
| Register | Name | Access | Value Range | Notes |
|----------|------|--------|-------------|-------|
| 0x07 | CPU_TEMP | RO | 0-100°C | Confirmed |
| 0xCA | AMBIENT_TEMP | RO | 0-100°C | Ambient/motherboard temp |
| 0xCE | CPU_FAN_DUTY | RW | 0-255 (0-100%) | Write via cmd=0x99 port=0x01 |
| 0xCF | GPU_FAN_DUTY | RW | 0-255 (0-100%) | Write via cmd=0x99 port=0x02 |
| 0xD0 | CPU_FAN_RPM_HI | RO | 0-255 | High byte of RPM counter |
| 0xD1 | CPU_FAN_RPM_LO | RO | 0-255 | Low byte of RPM counter |
| 0xD2 | GPU_FAN_RPM_HI | RO | 0-255 | High byte of RPM counter |
| 0xD3 | GPU_FAN_RPM_LO | RO | 0-255 | Low byte of RPM counter |
| 0xFB | GPU_TEMP | RO | 0-100°C | **Correct GPU temperature register** |

## Verified EC Commands
| Command | Port | Value | Effect |
|---------|------|-------|--------|
| 0x99 | 0x01 | 0x00-0xFF | Set CPU fan duty cycle |
| 0x99 | 0x02 | 0x00-0xFF | Set GPU fan duty cycle |
| 0x99 | 0xFF | - | Return to AUTO mode (both fans) |
| 0x80 | - | - | Read EC register |

## RPM Calculation

RPM = 2156220 / ((high << 8) + low)

### Tested Values (GPU Fan)
| Duty (hex) | Duty (%) | RPM |
|------------|----------|-----|
| 0x10 | 6% | 338 |
| 0x40 | 25% | 1395 |
| 0x80 | 50% | 2623 |
| 0xC0 | 75% | 3648 |
| 0xFF | 100% | 4577 |

## Module Status
Currently loaded modules (may conflict with direct ioperm access):

clevo_acpi 20480 0
tuxedo_io 24576 0
tuxedo_keyboard 122880 2 clevo_acpi,tuxedo_io
tuxedo_compatibility_check 12288 1 tuxedo_keyboard


To use direct ioperm access (your utility), unload these modules:
```bash
sudo modprobe -r tuxedo_io tuxedo_keyboard tuxedo_compatibility_check clevo_acpi
```

## Quick Test Commands

Summary of Discoveries

    GPU Temperature Register: 0xFB (previously thought to be 0xCD, which always returns 0)

    Fan Control: Works via command 0x99 with port 0x01 (CPU) and 0x02 (GPU)

    RPM Formula: Confirmed with actual measurements

    Module Conflict: tuxedo_io/clevo_acpi modules block direct ioperm access
    

---

### Файл: `EC_REGISTERS.txt`

```text
================================================================================
                    EC REGISTERS MAP - Clevo N960KPx
================================================================================

Generated: 2026-03-21
Based on: Hardware testing with gpu_fan_test_fixed
Kernel: 6.12.74+deb13+1-amd64

================================================================================
TEMPERATURE REGISTERS
================================================================================

0x07    CPU_TEMP                    49°C    (RO) - Confirmed working
0xCA    AMBIENT_TEMP                60°C    (RO) - Ambient/Motherboard temp
0xFB    GPU_TEMP                    43°C    (RO) - CORRECT register (matches nvidia-smi)
0xCD    0x00                        Always 0 - NOT used for GPU temp

================================================================================
FAN CONTROL REGISTERS
================================================================================

0xCE    CPU_FAN_DUTY                0x3D (23%)  (RW) - Write via cmd=0x99 port=0x01
0xCF    GPU_FAN_DUTY                0x00 (0%)   (RW) - Write via cmd=0x99 port=0x02

0xD0    CPU_FAN_RPM_HI              0x06        (RO)
0xD1    CPU_FAN_RPM_LO              0x09        (RO)
        CPU_RPM_raw = 0x0609 = 1545
        CPU_RPM = 2156220 / 1545 = 1396 RPM

0xD2    GPU_FAN_RPM_HI              0x00        (RO)
0xD3    GPU_FAN_RPM_LO              0x00        (RO)
        GPU_RPM = 0 RPM (fan stopped)

================================================================================
EC COMMANDS (via ports 0x62/0x66)
================================================================================

Command 0x99 with port:
    - 0x01 : Set CPU fan duty (value 0x00-0xFF)
    - 0x02 : Set GPU fan duty (value 0x00-0xFF)
    - 0xFF : Return to AUTO mode (both fans)

Command 0x80 : Read EC register
    - Write register address to 0x62, read value from 0x62

================================================================================
RPM FORMULA
================================================================================

RPM = 2156220 / ((high_byte << 8) + low_byte)

Tested values (GPU fan):
    Duty 0x10 (6%)   → raw 0x?    → 338 RPM
    Duty 0x40 (25%)  → raw 0x???  → 1395 RPM
    Duty 0x80 (50%)  → raw 0x???  → 2623 RPM
    Duty 0xC0 (75%)  → raw 0x???  → 3648 RPM
    Duty 0xFF (100%) → raw 0x???  → 4577 RPM

================================================================================
MODULE CONFLICT INFORMATION
================================================================================

Currently loaded modules that may affect direct EC access:
    clevo_acpi              - Provides ACPI interface
    tuxedo_io               - Provides tuxedo I/O interface
    tuxedo_keyboard         - Keyboard backlight control
    tuxedo_compatibility_check

To use direct ioperm access (your utility), unload these modules:
    sudo modprobe -r tuxedo_io tuxedo_keyboard tuxedo_compatibility_check clevo_acpi

To use tuxedo drivers (TCC), keep them loaded and use sysfs interface.

================================================================================
ACTIVE REGISTER VALUES (at time of dump)
================================================================================

Timestamp: 2026-03-21 22:10:30
CPU Temp: 49°C
GPU Temp: 43°C (from register 0xFB)
Ambient Temp: 60°C

CPU Fan: 23% (0x3D) @ 1396 RPM
GPU Fan: 0% (0x00) @ 0 RPM
```
