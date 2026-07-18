const express = require('express');
const http = require('http');
const path = require('path');
const os = require('os');

// OneNET credentials (server-side only, not exposed to client)
const ONENET_TOKEN = 'version=2018-10-31&res=products%2F8Fph2Xgid0%2Fdevices%2FAQMV2&et=1812214000&method=md5&sign=qS7KXVoPlPwMgEn7Ijh%2F6A%3D%3D';
const ONENET_API = 'https://iot-api.heclouds.com';

// ===== Web Server =====
const app = express();
const server = http.createServer(app);
const HTTP_PORT = 3000;

app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// ===== OneNET API 代理（隐藏 Token，避免客户端暴露） =====
async function proxyOneNET(req, res, baseUrl) {
  try {
    const url = ONENET_API + baseUrl;
    const isGet = req.method === 'GET';
    const fetchOptions = {
      method: req.method,
      headers: { 'authorization': ONENET_TOKEN }
    };
    if (!isGet) {
      fetchOptions.headers['Content-Type'] = 'application/json';
      fetchOptions.body = JSON.stringify(req.body);
    }

    // Build URL with query params
    const queryString = new URLSearchParams(req.query).toString();
    const fullUrl = queryString ? url + (url.includes('?') ? '&' : '?') + queryString : url;

    const response = await fetch(fullUrl, fetchOptions);
    const data = await response.json();
    res.json(data);
  } catch (e) {
    console.error('[Proxy] Error:', e.message);
    res.status(502).json({ code: -1, msg: 'Proxy error: ' + e.message });
  }
}

// 设备详情代理（仅返回 status/name/last_time，过滤敏感字段）
app.get('/api/device/detail', async (req, res) => {
  try {
    const url = ONENET_API + '/device/detail?' + new URLSearchParams(req.query).toString();
    const response = await fetch(url, { headers: { 'authorization': ONENET_TOKEN } });
    const data = await response.json();
    if (data.code === 0 && data.data) {
      data.data = {
        status: data.data.status,
        name: data.data.name,
        last_time: data.data.last_time
      };
    }
    res.json(data);
  } catch (e) {
    res.status(502).json({ code: -1, msg: 'Proxy error: ' + e.message });
  }
});

app.get('/api/thingmodel/query-device-property', (req, res) => proxyOneNET(req, res, '/thingmodel/query-device-property'));
app.post('/api/thingmodel/set-device-property',  (req, res) => proxyOneNET(req, res, '/thingmodel/set-device-property'));
app.get('/api/thingmodel/query-device-property-history', (req, res) => proxyOneNET(req, res, '/thingmodel/query-device-property-history'));

function getLocalIP() {
  const nets = os.networkInterfaces();
  for (const n of Object.keys(nets)) {
    for (const i of nets[n]) {
      if (i.family === 'IPv4' && !i.internal &&
          (n.toLowerCase().includes('wlan') || n.toLowerCase().includes('wi-fi')))
        return i.address;
    }
  }
  return 'localhost';
}

server.listen(HTTP_PORT, '0.0.0.0', () => {
  const ip = getLocalIP();
  console.log('===========================================');
  console.log('   AQM 智慧环境监测 - 仪表盘');
  console.log('===========================================');
  console.log('   网页: http://localhost:' + HTTP_PORT);
  console.log('        http://' + ip + ':' + HTTP_PORT);
  console.log('   固件直连 OneNET MQTT，此服务仅代理网页 API');
  console.log('===========================================');
});
