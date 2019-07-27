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
            console.info('successfully transmitted value: ', response.statusCode);
        } else {
            console.error('error while transmitting value: ', response.statusCode);
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
            let wattHours = parseInt(message.toString());
            console.info('Got', wattHours, 'watt hours from', clientId);
            sendValue('watthours_total', clientId, wattHours);
            break;
        case 'watts':
            let watts = parseFloat(message.toString());
            console.info('Got', watts, 'watts from', clientId);
            sendValue('watts', clientId, watts);
            break;
        case 'dead':
            console.info('client', clientId, 'died with message', message.toString());
            break;
        default:
            console.warn('unknown messageType', messageType);
            break;
    }
});