> **Note:** The links and images in this document may point to external sources. Please review them before opening.

# 📺 TV Scheduler & IoT Smart Switch

An industrial-grade, secure, and resilient IoT appliance controller built on **Zephyr RTOS 4.4** and target-configured for **Espressif ESP32** microcontrollers. This solution features a lightweight, mobile-first, local Web Configuration Dashboard that allows users to adjust device settings, manage complex recurring schedules, securely update firmware, and command reboots remotely.

---

## 🚀 Architectural Overview & Key Features

This repository implements a robust, defensive embedded design that moves away from standard "happy-path" implementations to handle transient errors, network drops, and power outages gracefully.

* **Hybrid Wi-Fi Manager (STA/AP Fallback):** Automatically boots into Station mode using stored credentials. If connection or DHCP fails within a 30-second window, it gracefully rolls back into a local Soft-AP (Access Point) hosting a captive provisioning server.
* **Segmented RESTful API Server:** Features fully isolated request and transmission buffers to prevent memory corruption and race conditions when processing concurrent network requests (e.g., rendering settings and schedules simultaneously).
* **Secure Settings Persistence:** Integrated with Zephyr's native Settings subsystem using a Non-Volatile Storage (NVS) wear-leveling backend. Includes automatic configuration schema version validation to handle firmware migration safely and masks raw credentials in console dumps.
* **Timezone & Cross-Midnight Scheduler:** Synchronizes system clock via SNTP with automatic backoffs and secondary server fallbacks (`pool.ntp.org`). Supports an efficient 8-bit weekday bitmask evaluation, timezone offset translations, and schedules that safely cross midnight (e.g., 22:00 to 02:00).
* **Defensive Hardware Abstraction (Relay & Button):** Low-level GPIO driver written with atomic software state guards and redundant retry loops to handle transient electrical noise.
* **Zero-Loss Safe Web Reboot:** The `/api/system/reset` endpoint schedules a 1-second one-shot kernel timer (`k_timer`), giving the networking stack ample time to transmit the final `HTTP 200 OK` response before executing a cold system reboot.
* **Physical Factory Restore Overrides:** Monitors the board's boot button in a background thread. Holding the button for 5 seconds clears all Wi-Fi credentials from NVS and reboots the system to enter provisioning mode immediately.
* **Direct Web-Stream OTA Update:** Directly flash signed binary update files (`zephyr.signed.bin`) chunk-by-chunk over HTTP POST streaming into MCUboot Slot 1, bypassing the need for heavy external CLI manager applications.

---

## 🐳 Development Environment Setup

This repository uses a pre-configured Docker container containing the Espressif toolchain, Zephyr SDK, and dependencies, ensuring fully reproducible builds.

### 1. Build the Development Container

To build the specialized Espressif Zephyr container environment:

```bash
docker build \
   --network=host \
   -t env-zephyr-espressif \
   -f Dockerfile.espressif .

```

> **Note:** `--network=host` is required to ensure the container has uninterrupted access to the internet during package installation.

### 2. Start the Development Container

Run the container in detached mode with your workspace mounted:

```bash
docker run -itd \
   --name zephyr-project \
   -p 2222:22 \
   -p 8800:8800 \
   -v "$(pwd)/workspace:/workspace" \
   -w /workspace \
   env-zephyr-espressif

```

> **Tip:** To use a non-persistent, ephemeral container instead, you can append `--rm` to the command above.

### 3. Access the Container Terminal

To drop into a bash prompt inside your running container:

```bash
docker exec -it zephyr-project /bin/bash

```

---

## 💻 Working with VS Code

To work inside the containerized environment using VS Code:

1. Launch VS Code on your host machine.
2. Connect to your local WSL environment if running on Windows.
3. Ensure the **Dev Containers** extension is installed.
4. Open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`) and select:
`Dev Containers: Attach to Running Container`
5. Select `/zephyr-project` (or `env-zephyr-espressif`) from the list.
6. Open `/workspace` from the container's directory browser.

---

## 🛠️ Building & Flashing

Once inside the development container environment, use the Zephyr `west` tool to compile and flash your firmware.

### 1. Initialization (First Time Setup)

Synchronize the Zephyr workspace files and verify modules:

```bash
west init -l /workspace
west update

```

### 2. Compiling the Application

Navigate to your application directory and compile for your Espressif target (e.g., `esp32_devkitc_wroom`):

Build using the build script from outside the container,
```bash
cd workspace/apps/Smart_Switch
docker exec -it zephyr-project /workspace/apps/Smart_Switch/build.sh
```

(or)

Enter into docker shell & run build,
```bash
docker exec -it zephyr-project /bin/bash
export ZEPHYR_BASE=/opt/toolchains/zephyr
cd /workspace/apps/Smart_Switch &&
  west build -b yd_esp32/esp32/procpu --sysbuild . --pristine -- -DCONFIG_ESP32_USE_UNSUPPORTED_REVISION=y -DDTC_OVERLAY_FILE=boards/esp32_devkitc.overlay
```

### 3. Flashing the Target

Connect your Espressif board to your host computer via USB, ensure the device is passed through to WSL/Docker, and execute:

```bash
west flash
```

You can monitor the real-time serial output of the device using your preferred serial terminal emulator (e.g., `minicom` or `picocom`) configured to **115200 baud**.

---

## Flashing with esptool

> **note** Identifying Flash Adresses
> To find out the flashing addresses, see mcuboot/zephyr/runners.yaml & smp_srv(project name)/zephyr/runners.yaml

mcuboot/zephyr/zephyr.bin is the bootloader that goes into 0x1000
mcuboot/zephyr/zephyr.signed.bin is the app that goes into slot 0 at 0x20000

> **note** Note
> Erase flash and flash when you switch between MCUBoot and non-MCUBoot

Install the required tools:

```bash
python -m pip install pyserial esptool
```

```bash
python -m esptool --chip esp32 erase-flash
```

```bash
python -m esptool --port "COM3" --chip esp32 --baud 921600 --before default-reset --after hard-reset write-flash -u --flash-size detect 0x1000 zephyr.bin
```

```bash
python -m esptool   --port COM3   --chip esp32   --baud 921600   --before default-reset   --after hard-reset   write-flash -u --flash-size detect 0x20000 zephyr.signed.bin
```

```bash
python -m serial.tools.miniterm COM3 115200

```
---

## 🔒 Production Security Configurations

Before deploying this device in the field, make sure to modify the following inside `config.c` and your system configuration:

* **Update default credentials:** Change the default Web UI Soft-AP password (`AP_PASSWORD` currently set to `"tvscheduler"`).
* **Enforce firmware signing:** Enforce signing of your update binaries via MCUboot private keys using:
```ini
CONFIG_MCUBOOT_SIGNATURE_KEY_FILE="root-rsa-2048.pem"

```


* **Disable debug logging:** Disable debug log modules (`CONFIG_LOG=n` or `CONFIG_LOG_DEFAULT_LEVEL=1`) inside your final production configuration to optimize flash usage and prevent reverse engineering via console output.
