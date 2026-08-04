# TV Scheduler

## To Dos

### Robustness 

fail safe relays
station timeouts to ap fallback
SNTP Retries

- Failure modes
- Robustness
- Retries

- HTTPS
  - Certificates
- OTA Update
  - UDP MCU Boot update 
- Reset from web page
- Refactoring
- Secure transfer for password
- Station mode connection failure handling


🚀 Phase 1: Fail-Safes & Connection Handling (Immediate Priority)
These items prevent the device from "bricking" or freezing when the outside world (WiFi router or NTP server) behaves unexpectedly.

1. Station Mode Connection Failure Handling & AP Fallback
The Issue: Right now, if the user changes their router password or the router is offline, your device will hang indefinitely at wifi_wait_for_ip_addr().

The Fix: Implement a connection timeout in your station-mode connection sequence.

Recommended Flow:

Try to connect to the saved SSID for 30 seconds.

If connection fails, automatically launch the local Access Point (AP Mode) so the user can reconfigure the device from the webpage without having to find the physical button.

2. Time Sync (SNTP) Robustness & Degraded Mode
The Issue: Currently, if time_sync() fails, main() exits with -1. This permanently freezes the TV scheduler if the NTP server experiences a temporary outage.

The Fix: Never crash the main loop due to network/time errors.

Degraded Mode Protocol:

If NTP fails or time is invalid, set a safe default state (e.g., TV ALLOWED/RELAY ON so the user can at least watch TV).

Keep checking NTP in the background every 5 minutes until time is successfully acquired, then activate the scheduler.

🔒 Phase 2: Security & Remote Administration (Medium Priority)
This phase addresses data security, memory boundaries, and remote user actions.

1. Reset from Web Page
Implementation Complexity: Very Low (1 Hour).

The Fix: Implement a POST /api/system/reset endpoint. When triggered:

Send a successful 200 OK JSON response back to the browser.

Start a 1-second one-shot kernel timer (k_timer).

When the timer expires, call sys_reboot(SYS_REBOOT_COLD). (The timer delay is critical to let the network stack finish sending the HTTP response before the chip cuts its power).

2. Robust JSON Input Validation
The Issue: Your custom simple JSON string parser (json_get_string) is vulnerable to buffer overflows if a malicious client sends extremely long strings or malformed JSON payloads.

The Fix: Replace custom string-searching with Zephyr’s built-in JSON Encoding/Decoding library (CONFIG_JSON_LIBRARY=y). It provides safe, schema-validated structure mapping automatically.

3. HTTPS & Secure Password Transfer
The Reality on Microcontrollers: Running full TLS (HTTPS) on an ESP32 or small MCU adds roughly 45KB to 60KB of RAM overhead (mbedTLS) and slows down page load speeds significantly due to cryptographic handshakes.

The Recommendation:

Alternative (Lightweight): If full HTTPS is too resource-heavy for your board, encrypt the sensitive payload (WiFi Password) inside the browser (app.js) using a public key cipher (like RSA or Curve25519) before POSTing it. The device then decodes it using its private key.

Native (Secure): If your board has ample RAM (>128KB free), enable CONFIG_HTTP_SERVER_SECURE=y and upload self-signed certificates.

☁️ Phase 3: Field Updates & Over-The-Air (OTA) (Future Priority)
This is the final phase before building physical enclosures or deploying multiple nodes.

1. OTA Update via MCUboot
The Issue: Custom UDP MCU boot scripts are highly vulnerable to network packet loss, corruption, and middle-man attacks.

The Zephyr standard: Integrate MCUboot (Zephyr's primary bootloader).

Recommended Path:

Enable SMP (Simple Management Protocol) over HTTP. This lets you upload firmware binaries directly through your existing web browser dashboard using Zephyr's built-in mcumgr library!

This is far more secure because MCUboot validates the cryptographic signature of the uploaded binary before executing it.

⚡ Recommended Refactoring Action Items (How to improve main.c today)
To make your main.c truly robust, move networking and synchronization out of the main thread. Your main loop should be simple, single-minded, and non-blocking.

Recommended Architecture:
           +---------------------------------------------+
           |                 System Boot                 |
           +---------------------------------------------+
                                  |
                                  v
           +---------------------------------------------+
           |       Spawn Background Worker Threads       |
           |  1. Button Monitor    2. Network / NTP Sync |
           +---------------------------------------------+
                                  |
                                  v
           +---------------------------------------------+
           |             Main Scheduler Loop             |
           |   (Runs constantly, checks local uptime     |
           |    and falls back to safe states if offline) |
           +---------------------------------------------+
This decoupled architecture ensures that even if Wi-Fi takes minutes to connect, or if the time server drops offline, your button and local override behaviors remain completely active and responsive.
