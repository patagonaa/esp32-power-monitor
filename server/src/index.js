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
    help: 'Total Power counted in Wh',
    labelNames: ['client_id']
});

let metrics = {};

function setWatthours(clientId, value) {
    metrics[clientId] = {
        value: value,
        timestamp: Date.now()
    };
}

function cleanupMetrics() {
    for (let client in metrics) {
        if (Date.now() > (metrics[client].timestamp + METRIC_MAX_AGE_SECONDS * 1000)) {
            delete metrics[client];
        }
    }
}

async function getMetrics() {
    client.register.resetMetrics();
    for (let client in metrics) {
        wattHoursGauge.set({'client_id': client}, metrics[client].value);
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
    mqttClient.subscribe('powermeter/+/watthours_total');
});

mqttClient.on('message', function (topic, message) {
    let topicSplit = topic.split('/');
    let clientId = topicSplit[1];
    setWatthours(clientId, parseInt(message.toString()));
});

setInterval(() => cleanupMetrics(), 60 * 1000);