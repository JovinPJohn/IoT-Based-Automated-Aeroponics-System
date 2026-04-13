#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Adafruit_TCS34725.h>
#include <LiquidCrystal.h>

// ─────────────────────────────────────────
//  WiFi CREDENTIALS
// ─────────────────────────────────────────
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "PASSWORD";

// ─────────────────────────────────────────
//  PINS
// ─────────────────────────────────────────
#define DHT_PIN       4
#define DHT_TYPE      DHT22
#define TRIG_PIN      18
#define ECHO_PIN      19
#define TDS_PIN       36
#define TCS_LED_PIN   23
#define MOTOR_IN1     5
#define MOTOR_ENA     13

// ─────────────────────────────────────────
//  RESERVOIR LEVELS  ← SET THESE TO YOUR TANK
//
//  HC-SR04 is mounted at the TOP of the tank.
//  Distance is small when water is HIGH (full).
//  Distance is large when water is LOW (empty).
//
//  How to measure:
//    1. Fill tank completely → read Serial → set RESERVOIR_TOP_CM
//    2. Empty to minimum safe level → read Serial → set RESERVOIR_LOW_CM
// ─────────────────────────────────────────
#define RESERVOIR_TOP_CM    5.0f    // distance (cm) when tank is 100% full
#define RESERVOIR_LOW_CM    20.0f   // distance (cm) when tank is too low — pump blocked

// ─────────────────────────────────────────
//  MISTING THRESHOLDS
// ─────────────────────────────────────────
#define HUMID_LOW       75.0f
#define TEMP_HIGH       28.0f
#define MIST_ON_MS      5000UL
#define MIST_COOLDOWN   20000UL

// ─────────────────────────────────────────
//  TDS RANGE
// ─────────────────────────────────────────
#define TDS_LOW_PPM     400
#define TDS_HIGH_PPM    1200

// ─────────────────────────────────────────
//  pH CALIBRATION  (R/B ratio → pH)
// ─────────────────────────────────────────
const float PH_TABLE[][2] = {
    {6.50f, 4.5f}, {5.00f, 5.0f}, {3.60f, 5.5f},
    {2.50f, 6.0f}, {1.70f, 6.5f}, {1.20f, 7.0f},
    {0.85f, 7.5f}, {0.55f, 8.0f}, {0.35f, 8.5f},
};
const int PH_POINTS = 9;

// ─────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────
DHT               dht(DHT_PIN, DHT_TYPE);
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
LiquidCrystal     lcd(14, 27, 26, 25, 33, 32);
WebServer         server(80);

// ─────────────────────────────────────────
//  SENSOR DATA
// ─────────────────────────────────────────
float    temperature  = 0;
float    humidity     = 0;
float    distanceCm   = 0;
int      waterPct     = 0;      // 0–100% fill level
int      tdsPpm       = 0;
float    phValue      = 0;
uint16_t r, g, b, c;
bool     isMisting    = false;
bool     reservoirOK  = false;

unsigned long lastMistEnd = 0;
unsigned long mistStart   = 0;
unsigned long lastSensor  = 0;
unsigned long lastLCD     = 0;
uint8_t       lcdPage     = 0;

// ─────────────────────────────────────────
//  WEB DASHBOARD
// ─────────────────────────────────────────
const char DASHBOARD[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Aeroponics Dashboard</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', sans-serif;
      background: #0f1923;
      color: #e0e0e0;
      min-height: 100vh;
      padding: 20px;
    }
    h1 {
      text-align: center;
      color: #4caf93;
      font-size: 1.6em;
      margin-bottom: 20px;
      letter-spacing: 2px;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(155px, 1fr));
      gap: 14px;
      max-width: 720px;
      margin: 0 auto;
    }
    .card {
      background: #1a2733;
      border-radius: 14px;
      padding: 18px 14px;
      text-align: center;
      border: 1px solid #2a3f50;
      transition: border-color 0.4s;
    }
    .card .icon  { font-size: 1.9em; margin-bottom: 6px; }
    .card .label { font-size: 0.72em; color: #7a9bb5; text-transform: uppercase; letter-spacing: 1px; }
    .card .value { font-size: 1.75em; font-weight: 700; color: #e8f4f0; margin: 5px 0 2px; }
    .card .unit  { font-size: 0.78em; color: #5a7a8a; }
    .card.warn   { border-color: #e07b39; }
    .card.ok     { border-color: #4caf93; }

    /* Reservoir bar */
    .tank-wrap {
      margin: 6px auto 0;
      width: 80%;
      height: 8px;
      background: #0f1923;
      border-radius: 4px;
      overflow: hidden;
    }
    .tank-fill {
      height: 100%;
      border-radius: 4px;
      transition: width 0.6s, background 0.4s;
    }

    .mist-badge {
      display: inline-block;
      margin: 18px auto 0;
      padding: 8px 26px;
      border-radius: 20px;
      font-weight: 700;
      letter-spacing: 1px;
      font-size: 0.92em;
    }
    .mist-on  { background:#1e4f3a; color:#4caf93; border:1px solid #4caf93; }
    .mist-off { background:#1e2a33; color:#7a9bb5; border:1px solid #2a4050; }
    .footer { text-align:center; margin-top:14px; font-size:0.7em; color:#2a4050; }
  </style>
</head>
<body>
  <h1>&#127807; AEROPONIC SYSTEM</h1>

  <div class="grid">

    <div class="card" id="card-temp">
      <div class="icon">&#127777;</div>
      <div class="label">Temperature</div>
      <div class="value" id="temp">--</div>
      <div class="unit">°C</div>
    </div>

    <div class="card" id="card-hum">
      <div class="icon">&#128167;</div>
      <div class="label">Humidity</div>
      <div class="value" id="hum">--</div>
      <div class="unit">%</div>
    </div>

    <div class="card" id="card-level">
      <div class="icon">&#128246;</div>
      <div class="label">Reservoir</div>
      <div class="value" id="level-pct">--</div>
      <div class="unit" id="level-cm">-- cm</div>
      <div class="tank-wrap">
        <div class="tank-fill" id="tank-bar" style="width:0%"></div>
      </div>
    </div>

    <div class="card" id="card-tds">
      <div class="icon">&#9878;</div>
      <div class="label">Nutrients</div>
      <div class="value" id="tds">--</div>
      <div class="unit">ppm</div>
    </div>

    <div class="card" id="card-ph">
      <div class="icon">&#9879;</div>
      <div class="label">pH</div>
      <div class="value" id="ph">--</div>
      <div class="unit">phenol red</div>
    </div>

  </div>

  <div style="text-align:center;">
    <span class="mist-badge" id="mist-badge">-- MIST --</span>
  </div>

  <div class="footer">Auto-refresh 3s &nbsp;|&nbsp; ESP32 Aeroponic Controller</div>

  <script>
    function fetchData() {
      fetch('/data')
        .then(r => r.json())
        .then(d => {

          // Temperature
          document.getElementById('temp').textContent = d.temp.toFixed(1);
          document.getElementById('card-temp').className = 'card ' + (d.temp > 28 ? 'warn' : 'ok');

          // Humidity
          document.getElementById('hum').textContent = d.hum.toFixed(1);
          document.getElementById('card-hum').className = 'card ' + (d.hum < 75 ? 'warn' : 'ok');

          // Reservoir — percentage + bar
          var pct = d.water_pct;
          document.getElementById('level-pct').textContent = pct + '%';
          document.getElementById('level-cm').textContent  = d.level.toFixed(1) + ' cm';
          var bar = document.getElementById('tank-bar');
          bar.style.width = pct + '%';
          bar.style.background = pct > 40 ? '#4caf93' : pct > 20 ? '#e0b03a' : '#e05a3a';
          document.getElementById('card-level').className  = 'card ' + (d.reservoir_ok ? 'ok' : 'warn');

          // TDS
          document.getElementById('tds').textContent = d.tds;
          document.getElementById('card-tds').className = 'card ' +
            (d.tds >= 400 && d.tds <= 1200 ? 'ok' : 'warn');

          // pH
          document.getElementById('ph').textContent = d.ph >= 0 ? d.ph.toFixed(2) : 'N/A';
          document.getElementById('card-ph').className = 'card ' +
            (d.ph >= 5.5 && d.ph <= 6.5 ? 'ok' : 'warn');

          // Mist badge
          var mb = document.getElementById('mist-badge');
          mb.textContent = d.misting ? '💧 MISTING ACTIVE' : '○  MIST IDLE';
          mb.className   = 'mist-badge ' + (d.misting ? 'mist-on' : 'mist-off');
        })
        .catch(() => {});
    }
    fetchData();
    setInterval(fetchData, 3000);
  </script>
</body>
</html>
)rawhtml";

// ─────────────────────────────────────────
//  HELPERS
// ─────────────────────────────────────────
float interpolatePH(float ratio) {
    if (ratio >= PH_TABLE[0][0])           return PH_TABLE[0][1];
    if (ratio <= PH_TABLE[PH_POINTS-1][0]) return PH_TABLE[PH_POINTS-1][1];
    for (int i = 0; i < PH_POINTS - 1; i++) {
        float hi = PH_TABLE[i][0], lo = PH_TABLE[i+1][0];
        if (ratio <= hi && ratio > lo) {
            float t = (ratio - hi) / (lo - hi);
            return PH_TABLE[i][1] + t * (PH_TABLE[i+1][1] - PH_TABLE[i][1]);
        }
    }
    return -1.0f;
}

// Converts sensor distance to fill percentage
// RESERVOIR_TOP_CM  → 100%  (sensor reading when full)
// RESERVOIR_LOW_CM  → 0%    (sensor reading when empty)
int distanceToPercent(float cm) {
    if (cm < 0) return 0;
    float pct = (RESERVOIR_LOW_CM - cm) /
                (RESERVOIR_LOW_CM - RESERVOIR_TOP_CM) * 100.0f;
    return (int)constrain(pct, 0, 100);
}

void runMotor()  { digitalWrite(MOTOR_IN1, HIGH); analogWrite(MOTOR_ENA, 220); }
void stopMotor() { digitalWrite(MOTOR_IN1, LOW);  analogWrite(MOTOR_ENA, 0);   }

// ─────────────────────────────────────────
//  SENSOR READS
// ─────────────────────────────────────────
void readAll() {
    // DHT22
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) humidity    = h;

    // HC-SR04
    digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long dur = pulseIn(ECHO_PIN, HIGH, 30000UL);
    distanceCm  = dur ? (dur * 0.0343f / 2.0f) : -1.0f;
    waterPct    = distanceToPercent(distanceCm);
    reservoirOK = (distanceCm > 0 && distanceCm < RESERVOIR_LOW_CM);

    // TDS
    long sum = 0;
    for (int i = 0; i < 10; i++) { sum += analogRead(TDS_PIN); delay(5); }
    float v = (sum / 10.0f / 4095.0f) * 3.3f;
    tdsPpm = (int)constrain(v * (1000.0f / 2.3f), 0, 9999);

    // TCS34725 + phenol red → pH
    tcs.getRawData(&r, &g, &b, &c);
    phValue = (b > 5) ? interpolatePH((float)r / (float)b) : -1.0f;
}

// ─────────────────────────────────────────
//  MISTING CONTROL
// ─────────────────────────────────────────
void controlMisting() {
    unsigned long now = millis();

    if (!isMisting) {
        bool cooled   = (now - lastMistEnd >= MIST_COOLDOWN);
        bool needMist = (humidity < HUMID_LOW || temperature > TEMP_HIGH);
        if (cooled && needMist && reservoirOK) {
            isMisting = true; mistStart = now;
            runMotor();
            Serial.printf("[PUMP] ON  — Hum:%.1f%%  Temp:%.1fC  Tank:%d%%\n",
                          humidity, temperature, waterPct);
        } else if (cooled && needMist && !reservoirOK) {
            Serial.println("[WARN] Misting needed but reservoir too low!");
        }
    } else {
        if (now - mistStart >= MIST_ON_MS) {
            isMisting = false; lastMistEnd = now;
            stopMotor();
            Serial.println("[PUMP] OFF");
        }
    }
}

// ─────────────────────────────────────────
//  LCD  — 5 rotating pages
// ─────────────────────────────────────────
void updateLCD() {
    lcd.clear();
    char r0[17], r1[17];
    switch (lcdPage % 5) {
        case 0:
            snprintf(r0, 17, "Temp: %5.1f C",  temperature);
            snprintf(r1, 17, "Hum:  %5.1f %%", humidity);
            break;
        case 1:
            // Show both fill % and raw cm
            snprintf(r0, 17, "Tank: %3d%%", waterPct);
            snprintf(r1, 17, "Dist: %4.1f cm", distanceCm);
            break;
        case 2:
            snprintf(r0, 17, "TDS: %4d ppm", tdsPpm);
            if      (tdsPpm < TDS_LOW_PPM)  snprintf(r1, 17, " ADD NUTRIENTS ");
            else if (tdsPpm > TDS_HIGH_PPM) snprintf(r1, 17, " DILUTE SOLN   ");
            else                             snprintf(r1, 17, " NORMAL RANGE  ");
            break;
        case 3:
            if (phValue < 0) {
                snprintf(r0, 17, "pH: READING...");
                snprintf(r1, 17, "Check phenolred");
            } else {
                snprintf(r0, 17, "pH:   %5.2f", phValue);
                snprintf(r1, 17, "%s",
                    (phValue >= 5.5f && phValue <= 6.5f)
                    ? "  OPTIMAL      " : "  ADJUST pH!  ");
            }
            break;
        case 4:
            snprintf(r0, 17, "Mist: %s", isMisting ? "  ACTIVE" : "  IDLE  ");
            snprintf(r1, 17, "IP:%s", WiFi.localIP().toString().c_str());
            break;
    }
    lcd.setCursor(0, 0); lcd.print(r0);
    lcd.setCursor(0, 1); lcd.print(r1);
    lcdPage++;
}

// ─────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);

    dht.begin();

    pinMode(TCS_LED_PIN, OUTPUT);
    digitalWrite(TCS_LED_PIN, HIGH);
    if (!tcs.begin()) Serial.println("[ERROR] TCS34725 not found");

    lcd.begin(16, 2);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(" AeroSystem v2  ");
    lcd.setCursor(0, 1); lcd.print(" Connecting WiFi");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(MOTOR_IN1, OUTPUT);
    pinMode(MOTOR_ENA, OUTPUT);
    stopMotor();

    // WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WiFi] Connecting");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500); Serial.print("."); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] http://%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Offline mode");
    }

    // Web routes
    server.on("/", []() {
        server.send_P(200, "text/html", DASHBOARD);
    });

    server.on("/data", []() {
        char json[256];
        snprintf(json, sizeof(json),
            "{\"temp\":%.1f,\"hum\":%.1f,"
            "\"level\":%.1f,\"water_pct\":%d,\"reservoir_ok\":%s,"
            "\"tds\":%d,\"ph\":%.2f,\"misting\":%s}",
            temperature, humidity,
            distanceCm, waterPct, reservoirOK ? "true" : "false",
            tdsPpm, phValue,
            isMisting ? "true" : "false");
        server.send(200, "application/json", json);
    });

    server.begin();

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("  AeroSystem  ");
    lcd.setCursor(0, 1); lcd.print("    READY     ");
    delay(1500);
    lcd.clear();
}

// ─────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────
void loop() {
    server.handleClient();

    unsigned long now = millis();

    if (now - lastSensor >= 2000UL) {
        readAll();
        lastSensor = now;
        Serial.printf(
            "[DATA] T:%.1f H:%.1f Tank:%d%%(%.1fcm) TDS:%d pH:%.2f\n",
            temperature, humidity, waterPct, distanceCm, tdsPpm, phValue);
    }

    if (now - lastLCD >= 3000UL) {
        updateLCD();
        lastLCD = now;
    }

    controlMisting();
}