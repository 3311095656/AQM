const mqtt = require('mqtt');
const { WebSocket } = require('ws');
const ws = new WebSocket('ws://127.0.0.1:3000');
ws.on('open', () => { console.log('[TEST] WS OK'); });
ws.on('message', (data) => { console.log('[TEST] WS:', data.toString().substring(0,80)); });
ws.on('error', (e) => { console.log('[TEST] WS ERR:', e.message); });
const mc = mqtt.connect('mqtt://127.0.0.1:8080', {clientId:'test-'+Math.random().toString(16).slice(2,8)});
mc.on('connect', () => {
  console.log('[TEST] MQTT OK');
  mc.publish('/8Fph2Xgid0/AQMV2/thing/property/post', JSON.stringify({
    id:'t',version:'1.0',params:{out_temperature:{value:25},out_humidity:{value:50},brightness:{value:100},in_temperature:{value:26},in_humidity:{value:55},gas_alarm:{value:false},window_state:{value:true},fan_state:{value:true},presence:{value:true},auto_mode:{value:true}}
  }), {qos:1});
  console.log('[TEST] Published');
});
setTimeout(() => { console.log('[TEST] Done'); process.exit(0); }, 2000);
