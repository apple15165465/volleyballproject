#include <M5Unified.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>

constexpr float BASE_THRESHOLD_DPS = 350.0f;
constexpr float RELEASE_RATIO = 0.55f;
constexpr uint32_t SAMPLE_INTERVAL_MS = 20;
constexpr uint32_t DEBOUNCE_MS = 800;
constexpr uint32_t RELEASE_HOLD_MS = 100;
constexpr float NOISE_ALPHA = 0.04f;
constexpr char SWING_LOG_PATH[] = "/swings.jsonl";
constexpr char AP_SSID[] = "SpikeLab-M5";
constexpr char AP_PASSWORD[] = "SpikeLab2026";

WebServer webServer(80);

float noiseFloorDps = 20.0f;
float swingPeakDps = 0.0f;
float displayPeakDps = 0.0f;
uint32_t swingStartedAt = 0;
uint32_t lastSwingAt = 0;
uint32_t belowThresholdAt = 0;
uint32_t lastSampleAt = 0;
uint32_t swingCount = 0;
bool swingActive = false;
bool flashReady = false;
bool awaitingSyncAck = false;
uint32_t pendingSyncCount = 0;
String serialCommand;

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#163d30"><title>Spike Lab</title>
<style>
:root{color-scheme:dark;--bg:#10231c;--panel:#19392d;--line:#315244;--text:#f1f5eb;--muted:#a6b8aa;--green:#8cdb9e;--orange:#ffad6b}*{box-sizing:border-box}body{margin:0;background:linear-gradient(145deg,#10231c,#172b35);color:var(--text);font:15px system-ui,'Noto Sans TC',sans-serif}.page{max-width:760px;margin:auto;padding:24px 16px 40px}.top{display:flex;justify-content:space-between;align-items:center;gap:12px;border-bottom:1px solid var(--line);padding-bottom:16px}.brand{font-size:12px;letter-spacing:1px;color:var(--green);font-weight:700}.title{margin:5px 0 0;font-size:23px}.live{font-size:12px;color:var(--green)}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin:16px 0}.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:15px;min-width:0}.label{font-size:11px;color:var(--muted)}.value{font-size:30px;font-weight:750;margin-top:8px;font-variant-numeric:tabular-nums}.unit{font-size:11px;color:var(--muted);margin-left:4px}.wide{margin-top:10px}.chart{height:210px;width:100%;display:block}.history{display:grid;gap:0;margin-top:8px}.row{display:grid;grid-template-columns:38px 1fr auto;gap:8px;padding:10px 0;border-top:1px solid var(--line);font-size:12px}.row span:first-child,.row span:last-child{color:var(--muted)}.hint{font-size:11px;color:var(--muted);margin-top:16px;line-height:1.6}.btn{border:1px solid var(--line);border-radius:6px;background:#203e32;color:var(--text);padding:8px 11px;font:inherit;font-size:12px}.empty{color:var(--muted);font-size:12px;padding:15px 0}@media(max-width:480px){.page{padding:18px 12px}.grid{gap:7px}.card{padding:12px 10px}.value{font-size:25px}.title{font-size:20px}.chart{height:180px}}
</style><body><main class="page"><header class="top"><div><div class="brand">SPIKE LAB / M5</div><h1 class="title">排球揮臂訓練</h1></div><span class="live">● 裝置即時連線</span></header><section class="grid"><article class="card"><div class="label">累積揮臂</div><div class="value" id="count">0</div></article><article class="card"><div class="label">平均峰值</div><div class="value"><span id="mean">0</span><span class="unit">deg/s</span></div></article><article class="card"><div class="label">動作變異</div><div class="value"><span id="cv">0</span><span class="unit">CV%</span></div></article></section><section class="card wide"><div class="label">揮臂角速度趨勢 / deg/s</div><canvas id="chart" class="chart"></canvas></section><section class="card wide"><div style="display:flex;justify-content:space-between;align-items:center"><div><b>最近紀錄</b><div class="label" style="margin-top:4px">資料保存在 M5Stick Flash</div></div><button class="btn" onclick="downloadCsv()">匯出 CSV</button></div><div id="history" class="history"><div class="empty">尚無揮臂記錄</div></div></section><p class="hint">本頁由 M5StickC Plus 提供，不需網際網路。完整 AI 分群與疲勞分析可在之後接回電腦版 Spike Lab 網站。</p></main>
<script>
const $=id=>document.getElementById(id);let latest=[];function draw(values){const c=$('chart'),r=c.getBoundingClientRect(),d=devicePixelRatio||1;c.width=r.width*d;c.height=r.height*d;const x=c.getContext('2d');x.scale(d,d);const w=r.width,h=r.height,p=24;x.clearRect(0,0,w,h);x.strokeStyle='#315244';x.fillStyle='#a6b8aa';x.font='10px system-ui';for(let i=0;i<4;i++){const y=p+(h-p*2)*i/3;x.beginPath();x.moveTo(p,y);x.lineTo(w-6,y);x.stroke()}if(!values.length)return;const max=Math.max(350,...values)*1.12,min=0;x.strokeStyle='#8cdb9e';x.lineWidth=2;x.beginPath();values.forEach((v,i)=>{const px=p+(w-p-8)*(values.length===1?.5:i/(values.length-1)),py=h-p-(h-p*2)*(v-min)/(max-min);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke();values.forEach((v,i)=>{const px=p+(w-p-8)*(values.length===1?.5:i/(values.length-1)),py=h-p-(h-p*2)*(v-min)/(max-min);x.fillStyle='#ffad6b';x.beginPath();x.arc(px,py,3,0,Math.PI*2);x.fill()})}function downloadCsv(){const lines=['id,timestamp,peak_dps,duration_ms',...latest.map((s,i)=>`${s.id},${s.timestamp},${s.peak_dps},${s.duration_ms}`)];const a=document.createElement('a');a.href=URL.createObjectURL(new Blob(['\ufeff'+lines.join('\n')],{type:'text/csv'}));a.download='volleyball_training.csv';a.click();URL.revokeObjectURL(a.href)}async function refresh(){try{const r=await fetch('/api/report');const d=await r.json();latest=d.swings||[];$('count').textContent=d.count;$('mean').textContent=Number(d.mean_dps).toFixed(1);$('cv').textContent=Number(d.cv_percent).toFixed(1);draw(latest.slice(-40).map(s=>Number(s.peak_dps)));$('history').innerHTML=latest.length?latest.slice(-8).reverse().map(s=>`<div class="row"><span>#${s.id}</span><span>${Number(s.peak_dps).toFixed(1)} deg/s</span><span>${Number(s.duration_ms).toFixed(0)} ms</span></div>`).join(''):'<div class="empty">尚無揮臂記錄</div>'}catch(e){}}addEventListener('resize',()=>draw(latest.slice(-40).map(s=>Number(s.peak_dps))));refresh();setInterval(refresh,1200);
</script></body></html>
)rawliteral";

float currentThresholdDps() {
  return max(BASE_THRESHOLD_DPS, noiseFloorDps * 3.5f);
}

void postSwing(uint32_t timestampMs, float peakDps, uint32_t durationMs) {
  if (!flashReady) {
    Serial.println("ERROR: flash storage unavailable");
    return;
  }

  StaticJsonDocument<192> payload;
  payload["timestamp"] = timestampMs;
  payload["peak_dps"] = peakDps;
  payload["duration_ms"] = durationMs;
  File logFile = LittleFS.open(SWING_LOG_PATH, FILE_APPEND);
  if (!logFile) {
    Serial.println("ERROR: cannot append swing log");
    return;
  }
  serializeJson(payload, logFile);
  logFile.println();
  logFile.close();
}

void sendPendingSwings() {
  if (!flashReady || awaitingSyncAck) {
    return;
  }

  File logFile = LittleFS.open(SWING_LOG_PATH, FILE_READ);
  pendingSyncCount = 0;
  if (logFile) {
    while (logFile.available()) {
      String line = logFile.readStringUntil('\n');
      line.trim();
      if (!line.isEmpty()) {
        Serial.println(line);
        pendingSyncCount++;
      }
    }
    logFile.close();
  }

  StaticJsonDocument<96> marker;
  marker["_sync"] = "complete";
  marker["count"] = pendingSyncCount;
  serializeJson(marker, Serial);
  Serial.println();
  awaitingSyncAck = true;
}

void handleSerialCommands() {
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n' || value == '\r') {
      serialCommand.trim();
      if (serialCommand == "SYNC") {
        sendPendingSwings();
      } else if (serialCommand == "ACK" && awaitingSyncAck) {
        File logFile = LittleFS.open(SWING_LOG_PATH, FILE_WRITE);
        if (logFile) {
          logFile.close();
          swingCount = 0;
          awaitingSyncAck = false;
        }
      }
      serialCommand = "";
    } else if (serialCommand.length() < 16) {
      serialCommand += value;
    }
  }
}

void drawDashboard(float speedDps) {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(8, 7);
  M5.Lcd.println("SPIKE LAB / HOTSPOT");
  M5.Lcd.drawFastHLine(8, 21, 224, DARKGREY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  M5.Lcd.setCursor(8, 29);
  M5.Lcd.println("SWINGS");
  M5.Lcd.setTextColor(TFT_GREEN, BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(8, 44);
  M5.Lcd.printf("%lu", static_cast<unsigned long>(swingCount));

  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(108, 29);
  M5.Lcd.println("PEAK / LIVE");
  M5.Lcd.setTextColor(TFT_ORANGE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(108, 48);
  M5.Lcd.printf("%.0f / %.0f", displayPeakDps, speedDps);

  M5.Lcd.setTextColor(LIGHTGREY, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(8, 82);
  M5.Lcd.printf("Threshold: %.0f d/s", currentThresholdDps());
  M5.Lcd.setCursor(8, 101);
  M5.Lcd.printf("WiFi: SpikeLab-M5 %s", flashReady ? "SAVED" : "NO FLASH");
  M5.Lcd.setCursor(8, 116);
  M5.Lcd.println("Phone: 192.168.4.1");
}

void handleReportRequest() {
  DynamicJsonDocument report(8192);
  JsonArray swings = report.createNestedArray("swings");
  float mean = 0.0f;
  float sumSquares = 0.0f;
  uint32_t count = 0;

  File logFile = LittleFS.open(SWING_LOG_PATH, FILE_READ);
  if (logFile) {
    while (logFile.available()) {
      String line = logFile.readStringUntil('\n');
      line.trim();
      if (line.isEmpty()) {
        continue;
      }
      StaticJsonDocument<192> item;
      if (deserializeJson(item, line)) {
        continue;
      }
      const float value = item["peak_dps"] | 0.0f;
      count++;
      const float delta = value - mean;
      mean += delta / count;
      sumSquares += delta * (value - mean);
      JsonObject output = swings.add<JsonObject>();
      output["id"] = count;
      output["timestamp"] = item["timestamp"];
      output["peak_dps"] = value;
      output["duration_ms"] = item["duration_ms"] | 0.0f;
    }
    logFile.close();
  }

  const float stdDev = count > 1 ? sqrtf(sumSquares / (count - 1)) : 0.0f;
  report["count"] = count;
  report["mean_dps"] = mean;
  report["std_dev_dps"] = stdDev;
  report["cv_percent"] = mean > 0.0f ? stdDev / mean * 100.0f : 0.0f;
  String body;
  serializeJson(report, body);
  webServer.send(200, "application/json; charset=utf-8", body);
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  webServer.on("/", HTTP_GET, []() {
    webServer.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
  });
  webServer.on("/api/report", HTTP_GET, handleReportRequest);
  webServer.onNotFound([]() {
    webServer.sendHeader("Location", "/", true);
    webServer.send(302, "text/plain", "");
  });
  webServer.begin();
}

void setup() {
  auto config = M5.config();
  M5.begin(config);
  M5.Imu.begin();
  M5.Lcd.setRotation(1);
  Serial.begin(115200);
  flashReady = LittleFS.begin(true);
  if (flashReady) {
    File logFile = LittleFS.open(SWING_LOG_PATH, FILE_READ);
    if (logFile) {
      while (logFile.available()) {
        String line = logFile.readStringUntil('\n');
        line.trim();
        if (!line.isEmpty()) {
          swingCount++;
        }
      }
      logFile.close();
    }
  }
  startAccessPoint();
  drawDashboard(0.0f);
}

void loop() {
  M5.update();
  webServer.handleClient();
  handleSerialCommands();
  const uint32_t now = millis();

  if (M5.BtnA.wasPressed()) {
    if (flashReady) {
      File logFile = LittleFS.open(SWING_LOG_PATH, FILE_WRITE);
      if (logFile) {
        logFile.close();
      }
    }
    swingCount = 0;
    displayPeakDps = 0.0f;
  }

  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleAt = now;

  float gx, gy, gz;
  M5.Imu.getGyroData(&gx, &gy, &gz);
  const float speedDps = sqrtf(gx * gx + gy * gy + gz * gz);
  const float thresholdDps = currentThresholdDps();

  if (!swingActive && speedDps < BASE_THRESHOLD_DPS) {
    noiseFloorDps = (1.0f - NOISE_ALPHA) * noiseFloorDps + NOISE_ALPHA * speedDps;
  }

  if (!swingActive && speedDps >= thresholdDps && now - lastSwingAt >= DEBOUNCE_MS) {
    swingActive = true;
    swingStartedAt = now;
    swingPeakDps = speedDps;
    belowThresholdAt = 0;
  }

  if (swingActive) {
    swingPeakDps = max(swingPeakDps, speedDps);
    if (speedDps < thresholdDps * RELEASE_RATIO) {
      if (belowThresholdAt == 0) {
        belowThresholdAt = now;
      } else if (now - belowThresholdAt >= RELEASE_HOLD_MS) {
        const uint32_t durationMs = now - swingStartedAt;
        swingCount++;
        lastSwingAt = now;
        displayPeakDps = swingPeakDps;
        postSwing(swingStartedAt, swingPeakDps, durationMs);
        swingActive = false;
        belowThresholdAt = 0;
      }
    } else {
      belowThresholdAt = 0;
    }
  }

  static uint32_t lastDisplayAt = 0;
  if (now - lastDisplayAt >= 150) {
    drawDashboard(speedDps);
    lastDisplayAt = now;
  }
}