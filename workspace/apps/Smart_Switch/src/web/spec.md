# 🌐 IoT Device Dashboard API Specification (v1.0)

This document defines the HTTP API interface contract between the Web Dashboard frontend and your **Zephyr 4.4 HTTP Server** backend. 

All interfaces communicate using **UTF-8 encoded JSON** payloads and expect standard HTTP status codes for response validation.

---

## 📋 Table of Contents

1. [General Specifications](#1-general-specifications)
2. [Device Settings Endpoints (`/api/settings`)](#2-device-settings-endpoints-apisettings)
3. [Schedules Endpoints (`/api/schedules`)](#3-schedules-endpoints-apischedules)
4. [Bitmask Definition (`day_mask`)](#4-bitmask-definition-day_mask)

---

## 1. General Specifications

*   **Base URL:** `/api`
*   **Media Type:** `application/json`
*   **Data Types Mapping:**
    *   `string`: Standard null-terminated character array on firmware.
    *   `integer`: Integer fields mapped to `uint8_t` or `uint16_t` to match your C definitions.
    *   `boolean`: Mapped to `bool` in C.

---

## 2. Device Settings Endpoints (`/api/settings`)

These endpoints read and write the general, system-wide configuration parameters of your IoT device.

### 📥 A. Get Device Settings

Retrieve the current general device parameters.

*   **Method:** `GET`
*   **URL:** `/api/settings`
*   **Response Headers:** `Content-Type: application/json`
*   **Status Codes:**
    *   `200 OK`: Request successful.
    *   `500 Internal Server Error`: Internal storage read failure.

#### 📝 Response Body Example:

```json
{
  "name": "Living Room Lamp",
  "wifi_ssid": "HomeWiFi_2G",
  "wifi_password": "supersecurepassword",
  "timezone": "America/New_York",
  "max_on_time_minutes": 120
}
```

---

### 📤 B. Update Device Settings

Write new general settings values to the device's persistent flash memory.

*   **Method:** `POST`
*   **URL:** `/api/settings`
*   **Request Headers:** `Content-Type: application/json`
*   **Status Codes:**
    *   `200 OK`: Settings successfully updated and committed to flash.
    *   `400 Bad Request`: Invalid parameters (e.g., string length too long, out-of-range integer).
    *   `500 Internal Server Error`: Flash write failure.

#### 📝 Request Body Example:

```json
{
  "name": "Bedroom Humidifier",
  "wifi_ssid": "Guest_Network",
  "wifi_password": "guestpassword123",
  "timezone": "Europe/London",
  "max_on_time_minutes": 60
}
```

---

## 3. Schedules Endpoints (`/api/schedules`)

These endpoints perform CRUD operations on the dynamic scheduler array (`schedules[8]` in firmware).

### 📥 A. List All Schedules

Retrieves an array of all active and configured schedules on the device.

*   **Method:** `GET`
*   **URL:** `/api/schedules`
*   **Response Headers:** `Content-Type: application/json`
*   **Status Codes:**
    *   `200 OK`: Request successful. Returns an array (even if empty `[]`).

#### 📝 Response Body Example:

```json
[
  {
    "enabled": true,
    "day_mask": 62,
    "start_hour": 8,
    "start_minute": 30,
    "end_hour": 17,
    "end_minute": 0
  },
  {
    "enabled": false,
    "day_mask": 65,
    "start_hour": 22,
    "start_minute": 0,
    "end_hour": 6,
    "end_minute": 30
  }
]
```

---

### 📤 B. Add a Schedule

Append a single new schedule to the end of the scheduler list.

*   **Method:** `POST`
*   **URL:** `/api/schedules`
*   **Request Headers:** `Content-Type: application/json`
*   **Status Codes:**
    *   `200 OK` / `210 Created`: Schedule successfully added.
    *   `400 Bad Request`: Payload validation failed (e.g., minutes $> 59$, hours $> 23$).
    *   `413 Payload Too Large`: Device schedule limit exceeded (Max 8 schedules).

#### 📝 Request Body Example:

```json
{
  "enabled": true,
  "day_mask": 31,
  "start_hour": 18,
  "start_minute": 0,
  "end_hour": 20,
  "end_minute": 15
}
```

---

### ✏️ C. Update an Existing Schedule

Modifies a specific schedule in the array using its 0-indexed position as a path parameter.

*   **Method:** `PUT`
*   **URL:** `/api/schedules/{index}` (e.g., `/api/schedules/0` to edit the first schedule)
*   **Request Headers:** `Content-Type: application/json`
*   **Status Codes:**
    *   `200 OK`: Schedule updated successfully.
    *   `400 Bad Request`: Invalid index or invalid time ranges.
    *   `404 Not Found`: No schedule exists at the provided index.

#### 📝 Request Body Example:
```json

{
  "enabled": true,
  "day_mask": 127,
  "start_hour": 6,
  "start_minute": 0,
  "end_hour": 8,
  "end_minute": 30
}
```

---

### 🗑️ D. Delete a Schedule

Remove a specific schedule from the array. The firmware should shift subsequent schedules down to close the gap and decrement `schedule_count`.

*   **Method:** `DELETE`
*   **URL:** `/api/schedules/{index}` (e.g., `/api/schedules/1` to delete the second schedule)
*   **Status Codes:**
    *   `200 OK`: Schedule deleted and list shifted successfully.
    *   `404 Not Found`: No schedule exists at the provided index.

---

## 4. Bitmask Definition (`day_mask`)

To optimize low-power transmissions and flash memory footprints on your microcontroller, the weekdays are represented using an **8-bit bitmask (`uint8_t`)**.

| Weekday | Bit Position | Binary Weight | Decimal Value |
| :--- | :--- | :--- | :--- |
| **Sunday** | Bit 0 | `0b00000001` | `1` |
| **Monday** | Bit 1 | `0b00000010` | `2` |
| **Tuesday** | Bit 2 | `0b00000100` | `4` |
| **Wednesday** | Bit 3 | `0b00001000` | `8` |
| **Thursday** | Bit 4 | `0b00010000` | `16` |
| **Friday** | Bit 5 | `0b00100000` | `32` |
| **Saturday** | Bit 6 | `0b01000000` | `64` |
| *Unused* | Bit 7 | `0b10000000` | `128` (Always `0`) |

### Examples:

*   **Weekdays Only (Mon - Fri):**  
    `2 + 4 + 8 + 16 + 32` = `62` (`0b00111110`)
*   **Weekends Only (Sat - Sun):**  
    `1 + 64` = `65` (`0b01000001`)
*   **Every Single Day:**  
    `1 + 2 + 4 + 8 + 16 + 32 + 64` = `127` (`0b01111111`)

---
