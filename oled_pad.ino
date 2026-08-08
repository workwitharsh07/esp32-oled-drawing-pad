/*
 * =============================================================================
 * Project    : ESP32 OLED Drawing Pad
 * Repository : esp32-oled-drawing-pad
 * Version    : 1.0.0
 * Author     : LifeTronix
 * License    : MIT License
 * =============================================================================
 *
 * A simple ESP32-based drawing pad that lets you draw directly on a 128×64
 * SSD1306 OLED display using an interactive input control.
 *
 * Hardware:
 * • ESP32 Development Board
 * • SSD1306 128×64 OLED Display (I2C)
 *
 * Wiring:
 * VCC → 3.3V
 * GND → GND
 * SDA → GPIO 21
 * SCL → GPIO 22
 *
 * Libraries:
 * • Adafruit GFX
 * • Adafruit SSD1306
 *
 * GitHub:
 * https://github.com/workwitharsh07/esp32-oled-drawing-pad
 *
 * =============================================================================
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <U8g2lib.h>
#include <Wire.h>

// ── Access Point config ───────────────────────────────────
#define AP_SSID  "OLED-Draw"   // WiFi name shown on your phone
#define AP_PASS  ""            // Empty = open network, no password
#define AP_IP    "192.168.4.1" // Fixed IP — always the same
// ──────────────────────────────────────────────────────────

// SH1106 128×64, I2C hardware pins (SDA=21, SCL=22)
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

AsyncWebServer server(80);
AsyncWebSocket  ws("/ws");

// 1-bit pixel buffer: 128 × 64 = 8192 bits = 1024 bytes
#define OLED_W 128
#define OLED_H  64
uint8_t pixelBuf[OLED_W * OLED_H / 8];

// ── Pixel buffer helpers ──────────────────────────────────

inline void setPixel(int x, int y) {
  if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
  int i = y * OLED_W + x;
  pixelBuf[i >> 3] |= (1 << (i & 7));
}

inline void clearPixel(int x, int y) {
  if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
  int i = y * OLED_W + x;
  pixelBuf[i >> 3] &= ~(1 << (i & 7));
}

inline bool getPixel(int x, int y) {
  if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return false;
  int i = y * OLED_W + x;
  return (pixelBuf[i >> 3] >> (i & 7)) & 1;
}

// Bresenham thick line into pixel buffer
void drawThickLine(int x0, int y0, int x1, int y1, int r) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    for (int ty = -r; ty <= r; ty++)
      for (int tx = -r; tx <= r; tx++)
        if (tx*tx + ty*ty <= r*r) setPixel(x0+tx, y0+ty);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void flushToOLED() {
  display.clearBuffer();
  for (int y = 0; y < OLED_H; y++)
    for (int x = 0; x < OLED_W; x++)
      if (getPixel(x, y)) display.drawPixel(x, y);
  display.sendBuffer();
}

// ── WebSocket event handler ───────────────────────────────

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo* info = (AwsFrameInfo*)arg;
  if (info->opcode != WS_TEXT) return;

  String msg((char*)data, len);

  if (msg == "CLEAR") {
    memset(pixelBuf, 0, sizeof(pixelBuf));
    flushToOLED();
    return;
  }

  // "LINE x0 y0 x1 y1 radius"  (canvas coords 320×160)
  if (msg.startsWith("L")) {
    int x0, y0, x1, y1, r;
    sscanf(msg.c_str(), "L %d %d %d %d %d", &x0, &y0, &x1, &y1, &r);
    // Map canvas (320×160) → OLED (128×64)
    x0 = x0 * OLED_W / 320;  y0 = y0 * OLED_H / 160;
    x1 = x1 * OLED_W / 320;  y1 = y1 * OLED_H / 160;
    r  = max(1, r * OLED_W / 320 / 2);
    drawThickLine(x0, y0, x1, y1, r);
    flushToOLED();
  }
}

// ── Embedded web page ─────────────────────────────────────

const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LifeTronix OLED Console</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
*{margin:0;padding:0;box-sizing:border-box}
:root{
--c:#00d9ff;
--bg:#04070d;
--card:#09131d;
--line:#143247;
--txt:#dffcff;
}
body{
font-family:'Share Tech Mono',monospace;
background:var(--bg);
color:var(--txt);
display:flex;
flex-direction:column;
align-items:center;
padding:20px;
overflow-x:hidden;
}
body:before{
content:"";
position:fixed;inset:0;
background-image:
linear-gradient(rgba(0,217,255,.08) 1px,transparent 1px),
linear-gradient(90deg,rgba(0,217,255,.08) 1px,transparent 1px);
background-size:40px 40px;
animation:grid 8s linear infinite;
pointer-events:none;
}
@keyframes grid{to{background-position:40px 40px}}
.header{
width:min(900px,100%);
padding:20px;
border:1px solid var(--line);
background:rgba(9,19,29,.85);
backdrop-filter:blur(12px);
border-radius:18px;
box-shadow:0 0 30px rgba(0,217,255,.18);
margin-bottom:18px;
text-align:center;
}
h1{letter-spacing:.25em;color:var(--c)}
.sub{opacity:.7;font-size:.8rem;margin-top:6px}
.panel{
width:min(900px,100%);
display:grid;
grid-template-columns:320px 1fr;
gap:18px;
}
.card{
background:rgba(9,19,29,.88);
border:1px solid var(--line);
border-radius:18px;
padding:14px;
box-shadow:0 0 25px rgba(0,217,255,.12);
}
.label{font-size:.7rem;color:var(--c);letter-spacing:.2em;margin-bottom:10px;text-align:center}
#preview{
width:100%;
height:auto;
image-rendering:pixelated;
background:#000;
border:2px solid #19384f;
border-radius:10px;
box-shadow:0 0 20px rgba(0,217,255,.25), inset 0 0 15px rgba(0,217,255,.15);
}
#canvas{
width:100%;
height:auto;
display:block;
background:#000;
border-radius:12px;
touch-action:none;
cursor:crosshair;
}
.controls{
width:min(900px,100%);
display:flex;
flex-wrap:wrap;
justify-content:center;
gap:12px;
margin-top:18px;
align-items:center;
}
.btn{
padding:10px 22px;
border-radius:12px;
background:#08131f;
border:1px solid rgba(0,217,255,.5);
color:var(--c);
font-family:inherit;
cursor:pointer;
transition:.25s;
}
.btn:hover{
background:var(--c);
color:#000;
box-shadow:0 0 20px rgba(0,217,255,.7);
transform:translateY(-2px);
}
input[type=range]{width:160px}
.status{
padding:10px 18px;
border-radius:999px;
border:1px solid var(--line);
}
.ok{color:#00ff88}
.err{color:#ff4d6d}
.footer{
margin-top:18px;
opacity:.6;
font-size:.75rem;
}
@media(max-width:760px){
.panel{grid-template-columns:1fr}
}
</style>
</head>
<body>
<div class="header">
<h1>OLED CONTROL CONSOLE</h1>
<div class="sub">ESP32 • SH1106 • LifeTronix Engineering</div>
</div>

<div class="panel">
<div class="card">
<div class="label">LIVE OLED</div>
<canvas id="preview" width="128" height="64"></canvas>
</div>

<div class="card">
<div class="label">DRAW AREA</div>
<canvas id="canvas" width="320" height="160"></canvas>
</div>
</div>

<div class="controls">
<label>Brush <input type="range" id="brush" min="2" max="22" value="8"></label>
<button class="btn" onclick="clearAll()">CLEAR</button>
<div id="status" class="status">CONNECTING...</div>
</div>

<div class="footer">LifeTronix • OLED Drawing Console v2</div>

<script>
const C=document.getElementById('canvas'),ctx=C.getContext('2d');
ctx.fillStyle='#000';ctx.fillRect(0,0,C.width,C.height);
ctx.lineCap=ctx.lineJoin='round';
const P=document.getElementById('preview'),pctx=P.getContext('2d');
pctx.fillStyle='#000';pctx.fillRect(0,0,128,64);
const brush=document.getElementById('brush');
const status=document.getElementById('status');
let ws,live=false;

function connect(){
 ws=new WebSocket(`ws://${location.host}/ws`);
 ws.onopen=()=>{live=true;status.textContent="CONNECTED";status.className="status ok";};
 ws.onclose=()=>{live=false;status.textContent="OFFLINE";status.className="status err";setTimeout(connect,2000);}
 ws.onerror=()=>{status.textContent="ERROR";status.className="status err";}
}
connect();
const send=m=>{if(live)ws.send(m);};

let draw=false,lx=0,ly=0;
function pos(e){
 const r=C.getBoundingClientRect();
 const s=e.touches?e.touches[0]:e;
 return{
 x:Math.round((s.clientX-r.left)*C.width/r.width),
 y:Math.round((s.clientY-r.top)*C.height/r.height)
 };
}
function line(x0,y0,x1,y1,size){
 ctx.strokeStyle="#00d9ff";
 ctx.lineWidth=size;
 ctx.beginPath();
 ctx.moveTo(x0,y0);
 ctx.lineTo(x1,y1);
 ctx.stroke();

 pctx.strokeStyle="#00d9ff";
 pctx.lineWidth=Math.max(1,size*128/320);
 pctx.beginPath();
 pctx.moveTo(x0*128/320,y0*64/160);
 pctx.lineTo(x1*128/320,y1*64/160);
 pctx.stroke();

 send(`L ${x0} ${y0} ${x1} ${y1} ${size}`);
}
function start(e){
e.preventDefault();
draw=true;
const p=pos(e);
lx=p.x;ly=p.y;
line(lx,ly,lx,ly,+brush.value);
}
function move(e){
if(!draw)return;
e.preventDefault();
const p=pos(e);
line(lx,ly,p.x,p.y,+brush.value);
lx=p.x;ly=p.y;
}
function end(){draw=false;}
["mousedown","touchstart"].forEach(v=>C.addEventListener(v,start,{passive:false}));
["mousemove","touchmove"].forEach(v=>C.addEventListener(v,move,{passive:false}));
["mouseup","mouseleave","touchend"].forEach(v=>C.addEventListener(v,end));
function clearAll(){
ctx.fillStyle="#000";ctx.fillRect(0,0,C.width,C.height);
pctx.fillStyle="#000";pctx.fillRect(0,0,128,64);
send("CLEAR");
}
</script>
</body>
</html>
)HTML";

// ── setup() ──────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  // ── Start OLED
  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(14, 24, "Starting AP...");
  display.sendBuffer();

  // ── Start Access Point (no password = open network)
  WiFi.softAP(AP_SSID, strlen(AP_PASS) ? AP_PASS : nullptr);
  delay(500);

  Serial.println("=== AP Ready ===");
  Serial.println("SSID : " AP_SSID);
  Serial.println("URL  : http://" AP_IP);

  // ── Show connection info on OLED
  display.clearBuffer();
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(0,  10, "WiFi: OLED-Draw");
  display.drawStr(0,  22, "No password needed");
  display.drawStr(0,  38, "Open browser:");
  display.drawStr(0,  52, "192.168.4.1");
  display.sendBuffer();
  delay(4000);

  // ── Ready screen
  memset(pixelBuf, 0, sizeof(pixelBuf));
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(10, 28, "OLED-Draw");
  display.drawStr(6,  44, "Draw on phone!");
  display.sendBuffer();

  // ── WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // ── Route: serve the page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", PAGE);
  });

  server.begin();
}

void loop() {
  ws.cleanupClients();
}
