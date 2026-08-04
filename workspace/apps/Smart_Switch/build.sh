#!/bin/bash
export ZEPHYR_BASE=/opt/toolchains/zephyr
cd /workspace/apps/Smart_Switch &&
  west build -b yd_esp32/esp32/procpu . --pristine -- -DCONFIG_ESP32_USE_UNSUPPORTED_REVISION=y -DDTC_OVERLAY_FILE=boards/esp32s3_devkitc.overlay
