# esp32-power-monitor
A project to get metrics from a simple power meter (with pulse output) over WiFi into Prometheus/Grafana.

PRs welcome!

## Architecture:
There is a client (ESP32) and server (Node.JS) part to this project. Communication is through MQTT, so a MQTT broker the server and client can connect to is required.

### Hardware
Development hardware consisted of:
- Housing with DIN-rail
- 16A GFCI/circuit breaker combination
- MeanWell 5V 2.4A PSU
- cheap single-phase power meter with pulse output (1000 pulses per kWh)
- DIN-Rail housing for the ESP32
- Neutrik PowerCon True1 Power-In and -Out connectors
- misc. installation material (spade connectors, wire, ferrules, ...)

I also added a quite low (1k) pullup in addition to the internal pullup on the pulse input of the ESP32 because it was triggering when there was a power spike (EMF). A bit of debouncing had to be added to the code because even then the ESP still triggered so this might work without the additional resistor.

![inside](./hardware/inside_small.jpg)
![outside](./hardware/outside_small.jpg)

(Later, the PowerCon input was replaced with a 16A CEE 3-phase plug and two PowerCon outputs and power meters were added so it can be used for power distribution with seperate measurement of the three phases)

### Client
The client sends its data to an MQTT Server for the server to subscribe to.
Currently the exported metrics are:
- total watt-hours measured
- current power usage calculated
- internal ESP32 temperature (offset can be ±10°C, also it is not terribly accurate)

Total power measured is also saved in the Flash of the ESP32 so it doesn't get lost after power loss. To initialize this value to your meters' reading, comment in `//writePulseCountEEPROM(xxx);` in `setup()` and replace `xxx` with your reading in watt-hours (be sure to comment this out again after running it!).

#### Configuration

`credentials.h`:
The client tries to subscribe to nearby Freifunk WiFi Networks, but you can configure a private WiFi network here as well.
MQTT Server credentials are configured here as well.

`config.cpp` / `config.h`:
There are a few variables to set here:
- `METER_NAME`: Client ID of this client (used for the MQTT topic)
- `STATS_SEND_INTERVAL`: temperature and uptime will be sent in this interval
- `meterConfigs`: multiple meters can be configured by increasing `METER_COUNT` and adding array entries
    - `meterPulsePin`: Pin where the pulse output of the power meter is connected to
    - `minPulseLength`: minimum pulse high/low time for debouncing (pulse is counted when the pulse has been high and then low for this amount of time)
    - `pulsesPerKilowattHour`

### Server
The server subscribes to the MQTT topics of all power meters and collects the metrics. A few Environment variables have to be set for this to work, the names should be self-explanatory. There is also a Dockerfile for both exporters, so each example configuration below is a `docker-compose.yml` file:

#### Prometheus
Exposes the metrics as Prometheus metrics.
```
version: "3"
services:
  esp32-powermonitor-exporter:
    restart: unless-stopped
    image: esp32-powermonitor-exporter
    build: ./esp32-power-monitor/server_prometheus # clone this git repo to ./esp32-power-monitor
    environment:
      - "MQTT_SERVER=mqtt://mqtt.example.com"
      - "MQTT_USER=powermeter"
      - "MQTT_PASSWORD=secretsecretsecret"
    ports:
      - "9400:3000"
```

#### InfluxDB
Sends the metrics to an InfluxDB Server.
```
version: "3"

services:
  powermeter-influxdb:
    restart: unless-stopped
    image: powermeter-influxdb
    build: ./esp32-power-monitor/server_influxdb # clone this git repo to ./esp32-power-monitor
    environment:
      - "MQTT_SERVER=mqtt://mqtt.example.com"
      - "MQTT_USER=powermeter"
      - "MQTT_PASSWORD=secretsecretsecret"
      - "INFLUX_SERVER=https://powermeter:secretsecretsecret@influxdb.example.com"
      - "INFLUX_INFLUX_DATABASE=powermeter"
```

## TODO?
- client-side
    - [x] support for multiple power meters per esp32 (e.g. three-phase)
    - [ ] MQTTS
