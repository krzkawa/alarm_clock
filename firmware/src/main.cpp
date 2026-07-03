#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#ifndef DISABLE_TFT
  #include <Arduino_GFX_Library.h>
#endif


#ifdef XXSR69_TEST
  constexpr uint8_t BTN_UP = 13;
  constexpr uint8_t BTN_DOWN = 14;
  constexpr uint8_t BTN_LEFT = 25;
  constexpr uint8_t BTN_OK = 26;
  constexpr uint8_t BTN_RIGHT = 27;
  constexpr uint8_t RELAY_PIN = 32;
#else
  constexpr uint8_t BTN_UP = 0;
  constexpr uint8_t BTN_DOWN = 1;
  constexpr uint8_t BTN_LEFT = 2;
  constexpr uint8_t BTN_OK = 4;
  constexpr uint8_t BTN_RIGHT = 5;
  constexpr uint8_t RELAY_PIN = 3;

  constexpr uint8_t TFT_BL = 6;
  constexpr uint8_t TFT_CS = 7;
  constexpr uint8_t TFT_DC = 8;
  constexpr uint8_t TFT_RST = 10;
  constexpr uint8_t TFT_MOSI = 20;
  constexpr uint8_t TFT_SCLK = 21;
#endif

constexpr bool RELAY_ACTIVE_HIGH = true;

int alarmHour = 7;
int alarmMinute = 0;

const char *AP_NAME = "AlarmClock";
const char *AP_PASSWORD = "wake-up-now";

#ifndef DISABLE_TFT
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7735(bus, TFT_RST, 1, true, 80, 160, 26, 1, 26, 1);
#endif
WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;
IPAddress apIp(192, 168, 4, 1);

struct ButtonState {
  uint8_t pin;
  bool lastReading;
  bool stable;
  uint32_t changedAt;
  bool longFired;
};

ButtonState buttons[] = {
  {BTN_UP, true, true, 0, false},
  {BTN_DOWN, true, true, 0, false},
  {BTN_LEFT, true, true, 0, false},
  {BTN_OK, true, true, 0, false},
  {BTN_RIGHT, true, true, 0, false},
};

uint32_t timeBaseMillis = 0;
uint32_t secondsAtBase = 7UL * 3600UL;
bool alarmEnabled = true;
bool alarmRinging = false;
bool alarmTriggeredToday = false;
bool snoozeActive = false;
int selectedDigit = 0;
int enteredCode[4] = {0, 0, 0, 0};
uint32_t lastDraw = 0;
uint32_t snoozeUntilSeconds = 0;
uint16_t activeUnlockCode = 0;
uint8_t selectedQuestions[3] = {0, 1, 2};
uint8_t activeMiniGame = 0;

enum UiMode {
  MODE_NORMAL,
  MODE_SET_CLOCK,
  MODE_SET_ALARM,
};

UiMode uiMode = MODE_NORMAL;
uint8_t selectedTimeField = 0;
constexpr uint32_t SNOOZE_SECONDS = 5UL * 60UL;
constexpr uint32_t LONG_PRESS_MS = 1200;

struct Question {
  const char *prompt;
  const char *answer;
  bool numeric;
};

const Question QUESTION_BANK[] = {
  {"What is 7 x 8?", "56", true},
  {"What is 12 + 15?", "27", true},
  {"What is 100 - 37?", "63", true},
  {"What is 9 x 6?", "54", true},
  {"What is 81 / 9?", "9", true},
  {"What is 14 + 19?", "33", true},
  {"What is 50 - 18?", "32", true},
  {"What is 6 x 7?", "42", true},
  {"What is 11 + 22?", "33", true},
  {"What is 90 / 10?", "9", true},
  {"What is 8 x 8?", "64", true},
  {"What is 45 + 12?", "57", true},
  {"What is 72 - 28?", "44", true},
  {"What is 5 x 13?", "65", true},
  {"What is 99 - 9?", "90", true},
  {"What is 16 + 17?", "33", true},
  {"What is 3 x 18?", "54", true},
  {"What is 120 / 12?", "10", true},
  {"What is 25 + 25?", "50", true},
  {"What is 70 - 41?", "29", true},
  {"Type WAKE in capitals.", "WAKE", false},
  {"Type ALARM in capitals.", "ALARM", false},
  {"Type CLOCK in capitals.", "CLOCK", false},
  {"Type MORNING in capitals.", "MORNING", false},
  {"Type READY in capitals.", "READY", false},
  {"Type FOCUS in capitals.", "FOCUS", false},
  {"Type AWAKE in capitals.", "AWAKE", false},
  {"Type START in capitals.", "START", false},
  {"Type LIGHT in capitals.", "LIGHT", false},
  {"Type TIMER in capitals.", "TIMER", false},
  {"How many minutes are in 1 hour?", "60", true},
  {"How many hours are in 1 day?", "24", true},
  {"How many days are in 1 week?", "7", true},
  {"How many seconds are in 1 minute?", "60", true},
  {"What number comes after 39?", "40", true},
  {"What number comes before 100?", "99", true},
  {"What is 2 to the power of 5?", "32", true},
  {"What is 15 x 4?", "60", true},
  {"What is 144 / 12?", "12", true},
  {"What is 13 + 31?", "44", true},
  {"What is 88 - 46?", "42", true},
  {"What is 17 + 18?", "35", true},
  {"What is 6 x 12?", "72", true},
  {"What is 96 / 8?", "12", true},
  {"What is 21 + 23?", "44", true},
  {"What is 75 - 26?", "49", true},
  {"What is 4 x 19?", "76", true},
  {"What is 18 + 27?", "45", true},
  {"What is 10 x 10?", "100", true},
  {"What is 111 - 22?", "89", true},
};

constexpr uint8_t QUESTION_COUNT = sizeof(QUESTION_BANK) / sizeof(QUESTION_BANK[0]);
constexpr uint8_t ACTIVE_QUESTION_COUNT = 3;
constexpr uint8_t MINI_GAME_COUNT = 5;

uint32_t secondsNow() {
  return secondsAtBase + ((millis() - timeBaseMillis) / 1000UL);
}

int currentHour() {
  return (secondsNow() / 3600UL) % 24;
}

int currentMinute() {
  return (secondsNow() / 60UL) % 60;
}

int currentSecond() {
  return secondsNow() % 60;
}

void setCurrentTime(int hour, int minute, int second = 0) {
  secondsAtBase = (uint32_t)constrain(hour, 0, 23) * 3600UL
    + (uint32_t)constrain(minute, 0, 59) * 60UL
    + (uint32_t)constrain(second, 0, 59);
  timeBaseMillis = millis();
  alarmTriggeredToday = false;
}

void createNewChallenge() {
  activeUnlockCode = random(1000, 10000);
  activeMiniGame = random(0, MINI_GAME_COUNT);

  for (uint8_t i = 0; i < ACTIVE_QUESTION_COUNT; i++) {
    bool unique = false;
    while (!unique) {
      selectedQuestions[i] = random(0, QUESTION_COUNT);
      unique = true;
      for (uint8_t j = 0; j < i; j++) {
        if (selectedQuestions[j] == selectedQuestions[i]) {
          unique = false;
          break;
        }
      }
    }
  }
}

void relayWrite(bool on) {
  digitalWrite(RELAY_PIN, on == RELAY_ACTIVE_HIGH ? HIGH : LOW);
}

String twoDigits(int value) {
  return value < 10 ? "0" + String(value) : String(value);
}

String timeText() {
  return twoDigits(currentHour()) + ":" + twoDigits(currentMinute()) + ":" + twoDigits(currentSecond());
}

String alarmText() {
  return twoDigits(alarmHour) + ":" + twoDigits(alarmMinute);
}

void resetEnteredCode() {
  for (int &digit : enteredCode) {
    digit = 0;
  }
  selectedDigit = 0;
}

int enteredCodeValue() {
  return enteredCode[0] * 1000 + enteredCode[1] * 100 + enteredCode[2] * 10 + enteredCode[3];
}

void stopAlarmIfCodeCorrect() {
  if (alarmRinging && enteredCodeValue() == activeUnlockCode) {
    alarmRinging = false;
    activeUnlockCode = 0;
    snoozeActive = false;
    relayWrite(false);
    resetEnteredCode();
  }
}

void snoozeAlarm() {
  if (!alarmRinging) {
    return;
  }

  alarmRinging = false;
  activeUnlockCode = 0;
  snoozeActive = true;
  snoozeUntilSeconds = secondsNow() + SNOOZE_SECONDS;
  relayWrite(false);
  resetEnteredCode();
}

bool buttonPressed(uint8_t index) {
  ButtonState &b = buttons[index];
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) {
    b.lastReading = reading;
    b.changedAt = millis();
  }
  if ((millis() - b.changedAt) > 35 && reading != b.stable) {
    b.stable = reading;
    if (b.stable == LOW) {
      b.longFired = false;
    }
    return b.stable == LOW;
  }
  return false;
}

bool buttonLongPressed(uint8_t index, uint32_t durationMs) {
  ButtonState &b = buttons[index];
  if (b.stable == LOW && !b.longFired && (millis() - b.changedAt) > durationMs) {
    b.longFired = true;
    return true;
  }
  return false;
}

void adjustClockTime(int amount) {
  int hour = currentHour();
  int minute = currentMinute();

  if (selectedTimeField == 0) {
    hour = (hour + amount + 24) % 24;
  } else {
    minute = (minute + amount + 60) % 60;
  }

  setCurrentTime(hour, minute, 0);
}

void adjustAlarmTime(int amount) {
  if (selectedTimeField == 0) {
    alarmHour = (alarmHour + amount + 24) % 24;
  } else {
    alarmMinute = (alarmMinute + amount + 60) % 60;
  }
  alarmTriggeredToday = false;
  snoozeActive = false;
}

void handleButtons() {
  if (alarmRinging) {
    if (buttonLongPressed(3, LONG_PRESS_MS)) {
      snoozeAlarm();
      return;
    }

    if (buttonPressed(0)) {
      enteredCode[selectedDigit] = (enteredCode[selectedDigit] + 1) % 10;
    }
    if (buttonPressed(1)) {
      enteredCode[selectedDigit] = (enteredCode[selectedDigit] + 9) % 10;
    }
    if (buttonPressed(2)) {
      selectedDigit = (selectedDigit + 3) % 4;
    }
    if (buttonPressed(3)) {
      stopAlarmIfCodeCorrect();
    }
    if (buttonPressed(4)) {
      selectedDigit = (selectedDigit + 1) % 4;
    }
    return;
  }

  if (buttonPressed(0)) {
    if (uiMode == MODE_SET_CLOCK) {
      adjustClockTime(1);
    } else if (uiMode == MODE_SET_ALARM) {
      adjustAlarmTime(1);
    }
  }
  if (buttonPressed(1)) {
    if (uiMode == MODE_SET_CLOCK) {
      adjustClockTime(-1);
    } else if (uiMode == MODE_SET_ALARM) {
      adjustAlarmTime(-1);
    }
  }
  if (buttonPressed(2)) {
    selectedTimeField = 0;
  }
  if (buttonPressed(3)) {
    uiMode = uiMode == MODE_NORMAL ? MODE_SET_CLOCK : (uiMode == MODE_SET_CLOCK ? MODE_SET_ALARM : MODE_NORMAL);
    selectedTimeField = 0;
  }
  if (buttonPressed(4)) {
    selectedTimeField = 1;
  }
}

void checkAlarm() {
  if (currentHour() == 0 && currentMinute() == 0 && currentSecond() < 2) {
    alarmTriggeredToday = false;
  }

  if (!alarmEnabled || alarmRinging || alarmTriggeredToday) {
    if (snoozeActive && !alarmRinging && secondsNow() >= snoozeUntilSeconds) {
      snoozeActive = false;
      alarmRinging = true;
      createNewChallenge();
      relayWrite(true);
    }
    return;
  }

  if (currentHour() == alarmHour && currentMinute() == alarmMinute) {
    alarmRinging = true;
    alarmTriggeredToday = true;
    createNewChallenge();
    relayWrite(true);
  }
}

void drawCentered(const String &text, int y, int size, uint16_t color) {
#ifdef DISABLE_TFT
  (void)text;
  (void)y;
  (void)size;
  (void)color;
#else
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;
  gfx->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx->setCursor((gfx->width() - w) / 2, y);
  gfx->print(text);
#endif
}

void drawScreen() {
#ifdef DISABLE_TFT
  static bool printed = false;
  if (!printed) {
    printed = true;
    Serial.println("TFT disabled for XXSR69 test build.");
    Serial.print("Connect to WiFi: ");
    Serial.println(AP_NAME);
    Serial.print("Open captive portal or http://");
    Serial.println(apIp);
  }
#else
  gfx->fillScreen(BLACK);
  drawCentered(timeText(), 6, 2, WHITE);

  gfx->setTextSize(1);
  gfx->setTextColor(CYAN);
  gfx->setCursor(4, 34);
  gfx->print("Alarm ");
  gfx->print(alarmText());
  gfx->print(alarmEnabled ? " ON" : " OFF");

  if (alarmRinging) {
    drawCentered("ALARM!", 50, 2, RED);
    gfx->setTextSize(2);
    for (int i = 0; i < 4; i++) {
      int x = 18 + i * 30;
      int y = 82;
      uint16_t bg = i == selectedDigit ? YELLOW : RGB565(45, 50, 58);
      uint16_t fg = i == selectedDigit ? BLACK : WHITE;
      gfx->fillRect(x - 4, y - 4, 22, 24, bg);
      gfx->setTextColor(fg);
      gfx->setCursor(x, y);
      gfx->print(enteredCode[i]);
    }
    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(4, 124);
    gfx->print("UP/DN digit OK enter");
    gfx->setCursor(4, 140);
    gfx->print("Hold OK = snooze");
  } else {
    if (uiMode == MODE_SET_CLOCK) {
      drawCentered("Set Clock", 54, 2, YELLOW);
    } else if (uiMode == MODE_SET_ALARM) {
      drawCentered("Set Alarm", 54, 2, YELLOW);
    } else if (snoozeActive) {
      drawCentered("Snoozed", 54, 2, 0xFD20);
    } else {
      drawCentered("Ready", 54, 2, GREEN);
    }

    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(4, 82);
    if (uiMode == MODE_NORMAL) {
      gfx->print("OK set clock/alarm");
      gfx->setCursor(4, 98);
      gfx->print("WiFi: ");
      gfx->print(AP_NAME);
      gfx->setCursor(4, 114);
      gfx->print("Pass: ");
      gfx->print(AP_PASSWORD);
      gfx->setCursor(4, 136);
      gfx->print("Site: 192.168.4.1");
    } else {
      gfx->print(selectedTimeField == 0 ? "Field: hour" : "Field: minute");
      gfx->setCursor(4, 98);
      gfx->print("UP/DN change");
      gfx->setCursor(4, 114);
      gfx->print("LEFT hour RIGHT min");
      gfx->setCursor(4, 136);
      gfx->print("OK next/save");
    }
  }
#endif
}

void setClockFromQuery() {
  if (server.hasArg("h") && server.hasArg("m")) {
    int h = constrain(server.arg("h").toInt(), 0, 23);
    int m = constrain(server.arg("m").toInt(), 0, 59);
    int s = server.hasArg("s") ? constrain(server.arg("s").toInt(), 0, 59) : 0;
    secondsAtBase = (uint32_t)h * 3600UL + (uint32_t)m * 60UL + (uint32_t)s;
    timeBaseMillis = millis();
    alarmTriggeredToday = false;
  }
}

void setAlarmFromQuery() {
  if (server.hasArg("ah") && server.hasArg("am")) {
    alarmHour = constrain(server.arg("ah").toInt(), 0, 23);
    alarmMinute = constrain(server.arg("am").toInt(), 0, 59);
    alarmTriggeredToday = false;
  }
  if (server.hasArg("enabled")) {
    alarmEnabled = server.arg("enabled") == "1";
  }
}

String pageHtml() {
  String html = R"HTML(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Alarm Unlock</title>
  <style>
    :root{color-scheme:dark;--bg:#07111b;--panel:#101826;--panel-2:#172132;--border:#263445;--text:#f3f7fc;--muted:#9aa8bb;--accent:#5ee7a8;--accent-2:#4bb6ff;--danger:#ff7b72;--shadow:0 16px 40px rgba(0,0,0,0.28)}
    *{box-sizing:border-box}
    body{font-family:Inter,system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;margin:0;background:linear-gradient(135deg,#07111b 0%,#0f1724 100%);color:var(--text)}
    main{max-width:620px;margin:auto;padding:24px 16px 40px}
    .shell{display:grid;gap:16px}
    .hero{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;padding:20px 22px;border:1px solid var(--border);border-radius:20px;background:linear-gradient(135deg,rgba(91,231,168,0.2),rgba(75,182,255,0.12));box-shadow:var(--shadow)}
    .eyebrow{margin:0 0 6px;font-size:0.74rem;letter-spacing:0.24em;text-transform:uppercase;color:var(--accent)}
    h1{margin:0 0 6px;font-size:1.6rem}
    .intro{margin:0;line-height:1.5;color:var(--muted);max-width:36ch}
    .status-pill{padding:8px 12px;border-radius:999px;background:rgba(255,255,255,0.08);border:1px solid rgba(255,255,255,0.12);font-size:0.86rem;color:#dce7f4;white-space:nowrap}
    .card{border:1px solid var(--border);border-radius:20px;padding:16px;background:rgba(16,24,38,0.96);box-shadow:var(--shadow)}
    .tab-row{display:flex;gap:8px;margin-bottom:12px}
    .tab{font:inherit;padding:10px 14px;border:1px solid transparent;border-radius:999px;background:#1d2a3a;color:#ebf2ff;font-weight:700;cursor:pointer;transition:all 0.2s ease}
    .tab.active{background:linear-gradient(135deg,var(--accent),#2ccf7d);color:#06110d;border-color:rgba(255,255,255,0.16)}
    .tab.secondary{background:#223042;color:#f3f7fc}
    .result-panel{min-height:58px;padding:12px 14px;border-radius:14px;border:1px solid rgba(255,255,255,0.08);background:var(--panel-2);margin:8px 0 14px;color:var(--muted)}
    .result-panel.success{border-color:rgba(94,231,168,0.35);background:rgba(94,231,168,0.1)}
    .result-panel.error{border-color:rgba(255,123,114,0.35);background:rgba(255,123,114,0.12)}
    .panel{display:grid;gap:12px}
    .panel h2{margin:0;font-size:1.05rem}
    .panel-header{display:flex;justify-content:space-between;align-items:center;gap:8px}
    label{display:block;margin:8px 0 4px;color:#dbe7f5;font-weight:600}
    input{font:inherit;padding:11px 12px;border-radius:10px;border:1px solid #334453;background:rgba(5,10,16,0.8);color:var(--text);width:100%;box-sizing:border-box;outline:none;transition:border-color .2s ease,box-shadow .2s ease}
    input:focus{border-color:var(--accent-2);box-shadow:0 0 0 3px rgba(75,182,255,0.16)}
    button{font:inherit;padding:10px 14px;border:0;border-radius:10px;background:linear-gradient(135deg,var(--accent),#2ed37b);color:#07120b;font-weight:800;margin:8px 6px 0 0;cursor:pointer;transition:transform .15s ease,box-shadow .15s ease}
    button:hover{transform:translateY(-1px)}
    button.secondary{background:#273547;color:#f3f7fc}
    button.wide{width:100%;margin:8px 0 0}
    .code{display:block;font-size:2.2rem;letter-spacing:0.34em;font-weight:800;color:var(--accent)}
    .muted{color:var(--muted)}
    canvas{width:100%;max-width:320px;height:auto;background:#081018;border:1px solid #334453;border-radius:14px;touch-action:none;display:block;margin:8px auto 0}
    .hidden{display:none}
    .pad{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin-top:10px}
    .pad button{min-width:56px;margin:0}
  </style>
</head>
<body>
<main>
  <div class="shell">
    <header class="hero">
      <div>
        <p class="eyebrow">Wake-up portal</p>
        <h1>Alarm unlock</h1>
        <p class="intro">Answer the prompts or beat the mini-game to reveal the 4-digit shutdown code.</p>
      </div>
      <div class="status-pill" id="statusPill">Questions</div>
    </header>

    <section class="card">
      <div class="tab-row">
        <button class="tab active" onclick="showMode('questions')">Questions</button>
        <button class="tab secondary" onclick="showMode('game')">Mini-game</button>
      </div>
      <div id="result" class="result-panel" aria-live="polite"></div>
      <div id="questions" class="panel">
        <div class="panel-header">
          <h2>Quick verification</h2>
          <span class="muted">Three prompts</span>
        </div>
        __QUESTIONS__
        <button class="wide" onclick="check()">Reveal code</button>
      </div>
      <div id="game" class="panel hidden">
        <div class="panel-header">
          <h2>Mini-game</h2>
          <span class="muted">Challenge mode</span>
        </div>
        <p id="gameText" class="muted"></p>
        <canvas id="gameCanvas" width="320" height="240"></canvas>
        <div id="gameButtons"></div>
      </div>
    </section>
  </div>
</main>
<script>
const code = "__CODE__";
const alarmActive = __ALARM_ACTIVE__;
const answers = [__ANSWERS__];
const activeGame = __GAME__;
let mode = 'questions';
let points = [];
let player = {x:150,y:212,w:20,h:20};
let cars = [];
let gameTimer = 0;

function setResult(message, tone) {
  const result = document.getElementById('result');
  result.className = 'result-panel' + (tone ? ' ' + tone : '');
  result.innerHTML = message;
}

function reveal() {
  setResult('<span class="code">' + code + '</span><p>Use this code to shut the alarm off.</p>', 'success');
}

function clearResult() {
  setResult('', '');
}

function showMode(next) {
  mode = next;
  document.querySelectorAll('.tab').forEach(btn => btn.classList.toggle('active', btn.textContent.trim().toLowerCase() === next));
  document.getElementById('questions').classList.toggle('hidden', next !== 'questions');
  document.getElementById('game').classList.toggle('hidden', next !== 'game');
  document.getElementById('statusPill').textContent = next === 'game' ? 'Mini-game' : 'Questions';
  clearResult();
  if (next === 'game') initGame();
}

function check() {
  if (!alarmActive) {
    setResult('No alarm is ringing right now. The code appears only when the alarm goes off.', 'error');
    return;
  }
  const ok = answers.every((answer, i) => document.getElementById('q' + i).value.trim() === answer);
  ok ? reveal() : setResult('One or more answers are wrong. Try again.', 'error');
}

function msg(text) {
  document.getElementById('gameText').textContent = text;
}

function targetPoints(game) {
  const pts = [];
  if (game === 0) {
    for (let i=0;i<96;i++) {
      const a = i * Math.PI * 2 / 96;
      pts.push([160 + Math.cos(a) * 70, 118 + Math.sin(a) * 70]);
    }
  } else if (game === 1) {
    addLine(160,45,75,190,pts); addLine(75,190,245,190,pts); addLine(245,190,160,45,pts);
  } else {
    const poly = [];
    for (let i=0;i<10;i++) {
      const r = i % 2 ? 30 : 78;
      const a = -Math.PI / 2 + i * Math.PI / 5;
      poly.push([160 + Math.cos(a) * r, 122 + Math.sin(a) * r]);
    }
    for (let i=0;i<10;i++) addLine(poly[i][0],poly[i][1],poly[(i+1)%10][0],poly[(i+1)%10][1],pts);
  }
  return pts;
}

function addLine(x1,y1,x2,y2,pts) {
  for (let i=0;i<=30;i++) {
    const t = i / 30;
    pts.push([x1 + (x2-x1)*t, y1 + (y2-y1)*t]);
  }
}

function nearestDistance(x,y,pts) {
  let best = 9999;
  for (const p of pts) {
    const d = Math.hypot(x-p[0], y-p[1]);
    if (d < best) best = d;
  }
  return best;
}

function drawTraceTemplate(game) {
  const c = document.getElementById('gameCanvas'), ctx = c.getContext('2d');
  const centers = [[160,118],[160,140],[160,122]];
  const center = centers[game] || [160,120];
  ctx.clearRect(0,0,320,240);
  ctx.fillStyle = '#a9b4c0';
  ctx.beginPath();
  ctx.arc(center[0], center[1], 4, 0, Math.PI * 2);
  ctx.fill();
}

function initDrawGame() {
  const names = ['circle','triangle','star'];
  msg('Draw a ' + names[activeGame] + ' around the dot. Need 93% accuracy.');
  clearResult();
  points = [];
  drawTraceTemplate(activeGame);
  const c = document.getElementById('gameCanvas'), ctx = c.getContext('2d');
  let drawing = false;
  const pos = e => {
    const r = c.getBoundingClientRect();
    const t = e.touches ? e.touches[0] : e;
    return [(t.clientX-r.left)*320/r.width, (t.clientY-r.top)*240/r.height];
  };
  c.onpointerdown = e => { drawing = true; points = []; clearResult(); drawTraceTemplate(activeGame); };
  c.onpointermove = e => {
    if (!drawing) return;
    const p = pos(e); points.push(p);
    ctx.fillStyle = '#43d17a'; ctx.fillRect(p[0]-2,p[1]-2,4,4);
  };
  c.onpointerup = () => {
    drawing = false;
    const target = targetPoints(activeGame);
    const avg = points.reduce((sum,p) => sum + nearestDistance(p[0],p[1],target), 0) / Math.max(points.length,1);
    const covered = target.filter(t => nearestDistance(t[0], t[1], points) < 16).length / target.length;
    const enoughInk = Math.min(1, points.length / 100);
    const closeness = Math.max(0, 1 - avg / 35);
    const score = Math.max(0, Math.min(100, Math.round((covered * 0.7 + closeness * 0.3) * enoughInk * 100)));
    if (score >= 93 && covered >= 0.9 && avg < 10) {
      reveal();
    } else {
      clearResult();
      msg('Accuracy: ' + score + '%. Try for 93%.');
    }
  };
}

function initCutGame() {
  msg('Cut the block in half. Draw one straight line through the middle.');
  const c = document.getElementById('gameCanvas'), ctx = c.getContext('2d');
  points = [];
  ctx.clearRect(0,0,320,240);
  ctx.fillStyle = '#233041'; ctx.fillRect(70,55,180,130);
  ctx.strokeStyle = '#43d17a'; ctx.lineWidth = 4;
  let drawing = false;
  const pos = e => {
    const r = c.getBoundingClientRect();
    return [(e.clientX-r.left)*320/r.width, (e.clientY-r.top)*240/r.height];
  };
  c.onpointerdown = e => { drawing = true; points = [pos(e)]; };
  c.onpointermove = e => { if (drawing) { const p=pos(e); points.push(p); ctx.lineTo(p[0],p[1]); ctx.stroke(); } };
  c.onpointerup = () => {
    drawing = false;
    if (points.length < 2) return;
    const a = points[0], b = points[points.length-1];
    const centerDist = nearestDistance(160,120,[a,b]);
    const midDist = Math.abs(((b[1]-a[1])*160 - (b[0]-a[0])*120 + b[0]*a[1] - b[1]*a[0]) / Math.max(1, Math.hypot(b[1]-a[1], b[0]-a[0])));
    const longEnough = Math.hypot(b[0]-a[0], b[1]-a[1]) > 145;
    (midDist < 10 && longEnough && centerDist > 50) ? reveal() : msg('Closer to the exact middle. Try again.');
  };
}

function initRoadGame() {
  msg('Hard mode: cross traffic, ride the platforms, reach the top.');
  const c = document.getElementById('gameCanvas'), ctx = c.getContext('2d');
  player = {x:150,y:218,w:16,h:16};
  cars = [
    {x:0,y:188,w:52,h:18,s:3.3,c:'#e84d4d'},
    {x:150,y:164,w:38,h:18,s:-3.9,c:'#ffb84d'},
    {x:40,y:140,w:70,h:18,s:4.6,c:'#e84d4d'},
    {x:250,y:116,w:44,h:18,s:-5.1,c:'#ffdf5d'},
    {x:90,y:92,w:58,h:18,s:5.4,c:'#e84d4d'},
    {x:210,y:68,w:76,h:18,s:-4.7,c:'#ffb84d'}
  ];
  const logs = [
    {x:10,y:42,w:74,h:18,s:2.1},
    {x:170,y:42,w:74,h:18,s:2.1},
    {x:80,y:18,w:58,h:18,s:-2.6},
    {x:250,y:18,w:58,h:18,s:-2.6}
  ];
  document.getElementById('gameButtons').innerHTML = '<div class="pad"><button onclick="moveP(0,-16)">Up</button><button onclick="moveP(-16,0)">Left</button><button onclick="moveP(16,0)">Right</button><button onclick="moveP(0,16)">Down</button></div>';
  clearInterval(gameTimer);
  gameTimer = setInterval(() => {
    ctx.clearRect(0,0,320,240);
    ctx.fillStyle = '#43d17a'; ctx.fillRect(0,0,320,14);
    ctx.fillStyle = '#17334d'; ctx.fillRect(0,14,320,52);
    ctx.fillStyle = '#243044'; ctx.fillRect(0,66,320,144);
    ctx.fillStyle = '#2c7a45'; ctx.fillRect(0,210,320,30);

    let onLog = false;
    logs.forEach(log => {
      log.x += log.s;
      if (log.x > 340) log.x = -log.w;
      if (log.x < -log.w - 20) log.x = 340;
      ctx.fillStyle = '#8b6f47';
      ctx.fillRect(log.x, log.y, log.w, log.h);
      if (player.x < log.x+log.w && player.x+player.w > log.x && player.y < log.y+log.h && player.y+player.h > log.y) {
        onLog = true;
        player.x += log.s;
      }
    });

    cars.forEach(car => {
      car.x += car.s;
      if (car.x > 340) car.x = -car.w;
      if (car.x < -car.w - 20) car.x = 340;
      ctx.fillStyle = car.c;
      ctx.fillRect(car.x, car.y, car.w, car.h);
      if (player.x < car.x+car.w && player.x+player.w > car.x && player.y < car.y+car.h && player.y+player.h > car.y) {
        player.x = 150; player.y = 218; msg('Hit. Back to start.');
      }
    });

    if (player.y < 66 && player.y >= 14 && !onLog) {
      player.x = 150; player.y = 218; msg('Water. Back to start.');
    }

    player.x = Math.max(0, Math.min(304, player.x));
    ctx.fillStyle = '#54a7ff'; ctx.fillRect(player.x, player.y, player.w, player.h);
    if (player.y < 10) { clearInterval(gameTimer); reveal(); }
  }, 28);
}

function moveP(dx,dy) {
  player.x = Math.max(0, Math.min(304, player.x + dx));
  player.y = Math.max(0, Math.min(224, player.y + dy));
}

function initGame() {
  if (!alarmActive) { msg('Mini-game appears only when the alarm is ringing.'); return; }
  clearInterval(gameTimer);
  document.getElementById('gameButtons').innerHTML = '';
  if (activeGame <= 2) initDrawGame();
  else if (activeGame === 3) initCutGame();
  else initRoadGame();
}
</script>
</body>
</html>
)HTML";

  String questionHtml;
  String answersJs;
  if (alarmRinging && activeUnlockCode != 0) {
    for (uint8_t i = 0; i < ACTIVE_QUESTION_COUNT; i++) {
      const Question &q = QUESTION_BANK[selectedQuestions[i]];
      questionHtml += "<label>";
      questionHtml += String(i + 1);
      questionHtml += ". ";
      questionHtml += q.prompt;
      questionHtml += "</label><input id=\"q";
      questionHtml += i;
      questionHtml += "\"";
      if (q.numeric) {
        questionHtml += " inputmode=\"numeric\"";
      }
      questionHtml += ">";

      if (i > 0) {
        answersJs += ",";
      }
      answersJs += "\"";
      answersJs += q.answer;
      answersJs += "\"";
    }
  } else {
    questionHtml = "<p class=\"muted\">The random questions and code appear here when the alarm is ringing.</p>";
  }

  html.replace("__QUESTIONS__", questionHtml);
  bool challengeActive = alarmRinging && activeUnlockCode != 0;
  html.replace("__CODE__", challengeActive ? String(activeUnlockCode) : String(""));
  html.replace("__ALARM_ACTIVE__", challengeActive ? String("true") : String("false"));
  html.replace("__ANSWERS__", answersJs);
  html.replace("__GAME__", String(activeMiniGame));
  return html;
}

void handleRoot() {
  server.send(200, "text/html", pageHtml());
}

void redirectToPortal() {
  server.sendHeader("Location", String("http://") + apIp.toString() + "/", true);
  server.send(302, "text/plain", "");
}

void handleAndroidCaptiveCheck() {
  redirectToPortal();
}

void handleSet() {
  setClockFromQuery();
  setAlarmFromQuery();
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random() ^ micros());

  pinMode(RELAY_PIN, OUTPUT);
  relayWrite(false);

  for (ButtonState &button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.lastReading = digitalRead(button.pin);
    button.stable = button.lastReading;
  }

#ifndef DISABLE_TFT
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx->begin();
  gfx->setRotation(1);
#endif

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_NAME, AP_PASSWORD);

  dnsServer.start(DNS_PORT, "*", apIp);

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/generate_204", handleAndroidCaptiveCheck);
  server.on("/gen_204", handleAndroidCaptiveCheck);
  server.on("/connecttest.txt", handleAndroidCaptiveCheck);
  server.on("/hotspot-detect.html", handleAndroidCaptiveCheck);
  server.onNotFound(redirectToPortal);
  server.begin();

  timeBaseMillis = millis();
  drawScreen();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  handleButtons();
  checkAlarm();

  if (millis() - lastDraw > 250) {
    lastDraw = millis();
    drawScreen();
  }
}
