# esp32-power-monitor
A project to get metrics from a simple power meter (with pulse output) over WiFi into Prometheus

## TODO
- [x] Client-Side
    - [x] expose metrics via MQTT
    - [x] auto-connect to network from a list
- [x] Server-Side
    - [x] MQTT subscriber that exposes power meter data as Prometheus metrics