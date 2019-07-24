const express = require('express')
const app = express();
const client = require('prom-client');
const mqtt = require('mqtt');

const MQTT_SERVER = process.env.MQTT_SERVER;
const MQTT_USER = process.env.MQTT_USER;
const MQTT_PASSWORD = process.env.MQTT_PASSWORD;
const METRIC_MAX_AGE_SECONDS = parseInt(process.env.METRIC_MAX_AGE_SECONDS || 300);

const wattHoursGauge = new client.Gauge({
    name: 'esp32_powermeter_watthours_total',
    help: 'Total Energy counted in Wh',
    labelNames: ['client_id']
});
const wattsGauge = new client.Gauge({
    name: 'esp32_powermeter_watts',
    help: 'Current Power in W',
    labelNames: ['client_id']
});

let metrics = {};

function setWatthours(clientId, value) {
    metrics[clientId] = {
        ...metrics[clientId],
        wattHours: value,
        wattHoursTime: Date.now(),
        timestamp: Date.now()
    };
}

function setWatts(clientId, value) {
    metrics[clientId] = {
        ...metrics[clientId],
        watts: value,
        wattsTime: Date.now(),
        timestamp: Date.now()
    };
}

function cleanupMetrics() {
    for (let client in metrics) {
        if (Date.now() > (metrics[client].timestamp + METRIC_MAX_AGE_SECONDS * 1000)) {
            console.info('removing', client, 'from active clients due to inactivity');
            delete metrics[client];
        }
    }
}

async function getMetrics() {
    client.register.resetMetrics();
    for (let client in metrics) {
        let metric = metrics[client];
        if (metric.wattHours !== undefined) {
            wattHoursGauge.set({ 'client_id': client }, metric.wattHours, metric.wattHoursTime);
        }
        if (metric.watts !== undefined) {
            wattsGauge.set({ 'client_id': client }, metric.watts, metric.wattsTime);
        }
    }
}

app.get('/metrics', async function (req, res, next) {
    try {
        getMetrics();
        res.end(client.register.metrics());
    } catch (e) {
        console.error(e);
        next(e);
    }
});

app.listen(3000, function () {
    console.log('Exporter listening on port 3000!')
});

const mqttClient = mqtt.connect(MQTT_SERVER, { username: MQTT_USER, password: MQTT_PASSWORD });

mqttClient.on('connect', function () {
    mqttClient.subscribe('powermeter/+/+');
});

mqttClient.on('message', function (topic, message) {
    let topicSplit = topic.split('/');
    let clientId = topicSplit[1];
    let metric = topicSplit[2];
    switch (metric) {
        case 'watthours_total':
            let wattHours = parseInt(message.toString());
            console.info('Got', wattHours, 'watt hours from', clientId);
            setWatthours(clientId, wattHours);
            break;
        case 'watts':
            let watts = parseFloat(message.toString());
            console.info('Got', watts, 'watts from', clientId);
            setWatts(clientId, watts);
            break;
        default:
            console.warn('unknown metric', metric);
            break;
    }
});

setInterval(() => cleanupMetrics(), 60 * 1000);