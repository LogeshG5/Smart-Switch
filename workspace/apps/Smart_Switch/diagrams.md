
## Operation Mode

```ascii


              ESP32 WiFi
                  |
        +---------+---------+
        |                   |
   STA interface       AP interface
        |                   |
  Home router          192.168.4.1


                 main.c
                    |
              wifi_manager
                    |
       +------------+------------+
       |                         |
   STA mode                 AP mode
       |                         |
 Internet time              HTTP config
       |                         |
 Scheduler              Save credentials

               Phone
                 │
         http://192.168.4.1
                 │
      ┌─────────────────────┐
      │   Zephyr HTTP Server│
      ├─────────────────────┤
      │ GET  /              │
      │ GET  /config        │
      │ POST /config        │
      │ POST /reboot        │
      └─────────────────────┘
                 │
          config.c
                 │
           scheduler.c

                 Browser
                    │
             Zephyr HTTP Server
                    │
      ┌─────────────┴─────────────┐
      │                           │
 Static Resources          Dynamic Resources
      │                           │
 index.html                GET /api/config
 style.css                 POST /api/config
 app.js                    GET /api/status
                            POST /api/reboot

                     Browser
                         │
                  HTTP GET/POST
                         │
                 Zephyr HTTP Server
                         │
        ┌────────────────┼────────────────┐
        │                │                │
   Static Files      REST API        Future OTA
        │                │                │
 index.html         /api/config      /api/update
 style.css          /api/status
 app.js             /api/reboot

```
```
```
