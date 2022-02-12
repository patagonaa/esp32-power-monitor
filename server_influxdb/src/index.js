const mqtt = require('mqtt');
const request = require('request');

const MQTT_SERVER = process.env.MQTT_SERVER || "mqtt://localhost";
const MQTT_USER = process.env.MQTT_USER;
const MQTT_PASSWORD = process.env.MQTT_PASSWORD;

const INFLUX_SERVER = process.env.INFLUX_SERVER || "http://localhost:8086";
const INFLUX_DATABASE = process.env.INFLUX_DATABASE || 'powermeter';

function getLine(measurement, tags, values, timestamp_ns) {
    let body = measurement;
    if (tags) {
        for (let tag in tags) {
            body += `,${tag}=${tags[tag]}`;
        }
    }
    body += ' ';
    let first = true;
    for (let field in values) {
        if (!first) {
            body += ',';
        } else {
            first = false;
        }
        body += `${field}=${values[field]}`;
    }

    if (timestamp_ns != null) {
        body += ` ${timestamp_ns}`;
    }
    return body;
}

function sendValue(measurement, tags, value) {
    let line = getLine(measurement, tags, { value: value });

    let requestOptions = {
        body: line
    };
    request.post(`${INFLUX_SERVER}/write?db=${INFLUX_DATABASE}`, requestOptions, (error, response, body) => {
        if (error) {
            console.error("error while transmitting value: ", error);
        } else if (response.statusCode >= 200 && response.statusCode < 300) {
            console.info('successfully transmitted value: HTTP', response.statusCode);
        } else {
            console.error('error while transmitting value: HTTP', response.statusCode);
        }
    });
}

const mqttClient = mqtt.connect(MQTT_SERVER, { username: MQTT_USER, password: MQTT_PASSWORD });

mqttClient.on('error', x => {
    console.log(x);
})

mqttClient.on('connect', () => {
    console.log('connected');
    mqttClient.subscribe('powermeter/+/+');
    mqttClient.subscribe('powermeter/+/+/+');
});

mqttClient.on('message', (topic, message) => {
    let topicSplit = topic.split('/');
    let clientId = topicSplit[1];
    let messageType = topicSplit[2];
    let phase = topicSplit[3] || 'missing';
    switch (messageType) {
        case 'watthours_total':
            let wattHours = parseFloat(message.toString());
            console.info('Got', wattHours, 'watt hours from', clientId, phase);
            sendValue('watthours_total', { clientid: clientId, phase: phase }, wattHours);
            break;
        case 'watts':
            let watts = parseFloat(message.toString());
            console.info('Got', watts, 'watts from', clientId, phase);
            sendValue('watts', { clientid: clientId, phase: phase }, watts);
            break;
        case 'temperature_c':
            let temperature = parseFloat(message.toString());
            console.info('Got', temperature, '°C from', clientId);
            sendValue('temperature', { clientid: clientId }, temperature);
            break;
        case 'uptime_ms':
            let uptime = parseFloat(message.toString());
            console.info('Got', uptime, 'ms uptime from', clientId);
            sendValue('uptime', { clientid: clientId }, uptime);
            break;
        case 'dead':
            console.info('client', { clientid: clientId }, 'died with message', message.toString());
            break;
        case 'up':
            console.info('client', { clientid: clientId }, 'connected with message', message.toString());
            break;
        default:
            console.warn('unknown messageType', messageType);
            break;
    }
});