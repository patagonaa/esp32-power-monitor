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

function removeMetrics(clientId){
    delete metrics[clientId];
}

function cleanupMetrics() {
    for (let clientId in metrics) {
        if (Date.now() > (metrics[clientId].timestamp + METRIC_MAX_AGE_SECONDS * 1000)) {
            console.info('removing', clientId, 'from active clients due to inactivity');
            removeMetrics(clientId);
        }
    }
}

async function getMetrics() {
    client.register.resetMetrics();
    for (let clientId in metrics) {
        let metric = metrics[clientId];
        if (metric.wattHours !== undefined) {
            wattHoursGauge.set({ 'client_id': clientId }, metric.wattHours, metric.wattHoursTime);
        }
        if (metric.watts !== undefined) {
            wattsGauge.set({ 'client_id': clientId }, metric.watts, metric.wattsTime);
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
    let messageType = topicSplit[2];
    switch (messageType) {
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
        case 'dead':
            console.info('client', clientId, 'died with message', message.toString());
            removeMetrics(clientId);
            break;
        default:
            console.warn('unknown messageType', messageType);
            break;
    }
});

setInterval(() => cleanupMetrics(), 60 * 1000);