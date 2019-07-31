const mqtt = require('mqtt');
const request = require('request');

const MQTT_SERVER = process.env.MQTT_SERVER || "mqtt://localhost";
const MQTT_USER = process.env.MQTT_USER;
const MQTT_PASSWORD = process.env.MQTT_PASSWORD;

const INFLUX_SERVER = process.env.INFLUX_SERVER || "http://localhost:8086";
const INFLUX_DATABASE = process.env.INFLUX_DATABASE || 'powermeter';

function sendValue(measurement, clientId, value) {
    let requestOptions = {
        body: `${measurement},clientid=${clientId} value=${value}`
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

mqttClient.on('connect', function () {
    mqttClient.subscribe('powermeter/+/+');
});

mqttClient.on('message', function (topic, message) {
    let topicSplit = topic.split('/');
    let clientId = topicSplit[1];
    let messageType = topicSplit[2];
    switch (messageType) {
        case 'watthours_total':
            let wattHours = parseFloat(message.toString());
            console.info('Got', wattHours, 'watt hours from', clientId);
            sendValue('watthours_total', clientId, wattHours);
            break;
        case 'watts':
            let watts = parseFloat(message.toString());
            console.info('Got', watts, 'watts from', clientId);
            sendValue('watts', clientId, watts);
            break;
        case 'temperature_c':
            let temperature = parseFloat(message.toString());
            console.info('Got', temperature, '°C from', clientId);
            sendValue('temperature', clientId, temperature);
            break;
        case 'uptime_ms':
            let uptime = parseFloat(message.toString());
            console.info('Got', uptime, 'ms uptime from', clientId);
            sendValue('uptime', clientId, uptime);
            break;
        case 'dead':
            console.info('client', clientId, 'died with message', message.toString());
            break;
        default:
            console.warn('unknown messageType', messageType);
            break;
    }
});