# esp32-power-monitor
A project to get metrics from a simple power meter (with pulse output) over WiFi into Prometheus

## TODO
- Client-Side
    - expose metrics via MQTT
    - auto-connect to network from a list
- Server-Side
    - MQTT subscriber that exposes power meter data as Prometheus metrics