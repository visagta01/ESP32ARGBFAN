#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// ===== CONFIG WIFI =====
const char* ssid = "TonSSID";
const char* password = "TonMotDePasse";

// ===== CONFIG VENTILATEURS =====
#define FAN_COUNT 3
int fanPins[FAN_COUNT] = {18, 19, 21};
int fanLeds[FAN_COUNT] = {12, 12, 12};

Adafruit_NeoPixel* fans[FAN_COUNT];

// ===== MODES =====
enum Mode {
  MODE_OFF = 0,
  MODE_STATIC,
  MODE_PULSE,
  MODE_PULSE_COLOR,
  MODE_COLOR_CYCLE,
  MODE_TIMER
};

// ===== ÉTAT PAR VENTILO =====
uint8_t modeFan[FAN_COUNT];
uint8_t R[FAN_COUNT], G[FAN_COUNT], B[FAN_COUNT];
bool fanEnabled[FAN_COUNT] = {true, true, true};

// ===== CONTRÔLE GLOBAL =====
bool globalControl = false;      // true = on contrôle tous les ventilos
bool globalEnabled = true;
uint8_t gR = 255, gG = 0, gB = 0;
uint8_t globalMode = MODE_STATIC;

// ===== HORLOGE MANUELLE + TIMER GLOBAL =====
int currentHour = 0, currentMinute = 0;
int startHour = 20, startMinute = 0;
int stopHour  = 23, stopMinute  = 0;

// ===== VITESSES (1–10, 1 = lent, 10 = rapide) =====
int pulseSpeed      = 5;
int pulseColorSpeed = 5;
int cycleSpeed      = 5;

// ===== ANIMATIONS =====
unsigned long lastAnim = 0;
unsigned long lastMinuteTick = 0;
int pulsePhase = 0;
bool pulseUp = true;
int cyclePos = 0;

WebServer server(80);

// ===== OUTILS LED =====
uint32_t wheel(byte pos) {
  if (pos < 85) return fans[0]->Color(pos * 3, 255 - pos * 3, 0);
  if (pos < 170) {
    pos -= 85;
    return fans[0]->Color(255 - pos * 3, 0, pos * 3);
  }
  pos -= 170;
  return fans[0]->Color(0, pos * 3, 255 - pos * 3);
}

void setFanColor(int id, uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < fanLeds[id]; i++) {
    fans[id]->setPixelColor(i, fans[id]->Color(r, g, b));
  }
  fans[id]->show();
}

// ===== HORLOGE =====
bool timeInRange(int h, int m, int sh, int sm, int eh, int em) {
  int now  = h * 60 + m;
  int start = sh * 60 + sm;
  int stop  = eh * 60 + em;
  if (stop >= start) {
    return now >= start && now < stop;
  } else {
    // plage qui passe minuit
    return now >= start || now < stop;
  }
}

void updateClock() {
  if (millis() - lastMinuteTick >= 60000) {
    lastMinuteTick = millis();
    currentMinute++;
    if (currentMinute >= 60) {
      currentMinute = 0;
      currentHour++;
      if (currentHour >= 24) currentHour = 0;
    }
  }
}

// ===== PAGE WEB AVEC CSS =====
String htmlPage() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>ESP32 ARGB Multi-Fans</title>
<style>
  body {
    font-family: Arial, sans-serif;
    background: #0b0c10;
    color: #c5c6c7;
    margin: 0;
    padding: 0;
  }
  .container {
    max-width: 900px;
    margin: 0 auto;
    padding: 20px;
  }
  h1, h2, h3, h4 {
    color: #66fcf1;
    text-align: center;
  }
  .card {
    background: #1f2833;
    border-radius: 8px;
    padding: 15px 20px;
    margin-bottom: 15px;
    box-shadow: 0 0 10px rgba(0,0,0,0.5);
  }
  .row {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
  }
  .col {
    flex: 1;
    min-width: 250px;
  }
  label {
    display: inline-block;
    margin: 5px 0;
  }
  input[type="number"], input[type="color"] {
    padding: 4px;
    margin: 3px 0;
    border-radius: 4px;
    border: 1px solid #45a29e;
    background: #0b0c10;
    color: #c5c6c7;
  }
  button, .btn-link {
    display: inline-block;
    padding: 6px 10px;
    margin: 4px 2px;
    border-radius: 4px;
    border: none;
    background: #45a29e;
    color: #0b0c10;
    cursor: pointer;
    text-decoration: none;
    font-size: 13px;
  }
  button:hover, .btn-link:hover {
    background: #66fcf1;
  }
  .badge {
    display: inline-block;
    padding: 2px 6px;
    border-radius: 4px;
    font-size: 11px;
    margin-left: 5px;
  }
  .badge-on { background: #45a29e; color: #0b0c10; }
  .badge-off { background: #c3073f; color: #fff; }
  hr {
    border: none;
    border-top: 1px solid #45a29e;
    margin: 15px 0;
  }
</style>
</head>
<body>
<div class="container">
<h1>ESP32 ARGB Multi‑Fan Controller</h1>
)";

  // Heure actuelle
  html += "<div class='card'><h3>Horloge & Timer</h3>";
  html += "<form action='/setTime' method='GET'>";
  html += "<label>Heure actuelle :</label><br>";
  html += "H: <input name='h' type='number' min='0' max='23' value='" + String(currentHour) + "'>";
  html += " M: <input name='m' type='number' min='0' max='59' value='" + String(currentMinute) + "'>";
  html += "<button type='submit'>Mettre à jour</button></form><br>";

  html += "<form action='/setWindow' method='GET'>";
  html += "<label>Plage horaire globale :</label><br>";
  html += "Début H: <input name='sh' type='number' min='0' max='23' value='" + String(startHour) + "'>";
  html += " M: <input name='sm' type='number' min='0' max='59' value='" + String(startMinute) + "'><br>";
  html += "Fin H: <input name='eh' type='number' min='0' max='23' value='" + String(stopHour) + "'>";
  html += " M: <input name='em' type='number' min='0' max='59' value='" + String(stopMinute) + "'>";
  html += "<button type='submit'>Valider</button></form>";
  html += "</div>";

  // Contrôle global
  html += "<div class='card'><h3>Contrôle global</h3>";
  html += "<p>Contrôle global : ";
  html += globalControl ? "<span class='badge badge-on'>ACTIF</span>" : "<span class='badge badge-off'>INACTIF</span>";
  html += " <a class='btn-link' href='/toggleGlobal?on=" + String(globalControl ? 0 : 1) + "'>";
  html += globalControl ? "Désactiver" : "Activer";
  html += "</a></p>";

  html += "<p>Tous les ventilateurs : ";
  html += globalEnabled ? "<span class='badge badge-on'>ON</span>" : "<span class='badge badge-off'>OFF</span>";
  html += " <a class='btn-link' href='/toggleAll?on=" + String(globalEnabled ? 0 : 1) + "'>";
  html += globalEnabled ? "Tout éteindre" : "Tout allumer";
  html += "</a></p>";

  html += "<label>Couleur globale :</label><br>";
  html += "<input type='color' id='gcolor'>";
  html += "<button type='button' onclick='setGlobalColor()'>Appliquer</button><br><br>";

  html += "<label>Mode global :</label><br>";
  html += "<a class='btn-link' href='/setGlobalMode?m=1'>Fixe</a>";
  html += "<a class='btn-link' href='/setGlobalMode?m=2'>Pulser</a>";
  html += "<a class='btn-link' href='/setGlobalMode?m=3'>Pulser+Couleur</a>";
  html += "<a class='btn-link' href='/setGlobalMode?m=4'>Cycle</a>";
  html += "<a class='btn-link' href='/setGlobalMode?m=5'>Timer</a>";
  html += "<a class='btn-link' href='/setGlobalMode?m=0'>OFF</a>";

  html += "<hr><form action='/setSpeeds' method='GET'>";
  html += "<label>Vitesse Pulser (1–10) :</label><br>";
  html += "<input name='ps' type='number' min='1' max='10' value='" + String(pulseSpeed) + "'><br>";
  html += "<label>Vitesse Pulser+Couleur (1–10) :</label><br>";
  html += "<input name='pcs' type='number' min='1' max='10' value='" + String(pulseColorSpeed) + "'><br>";
  html += "<label>Vitesse Cycle Couleur (1–10) :</label><br>";
  html += "<input name='cs' type='number' min='1' max='10' value='" + String(cycleSpeed) + "'><br>";
  html += "<button type='submit'>Appliquer vitesses</button></form>";
  html += "</div>";

  // Contrôle individuel
  html += "<div class='card'><h3>Contrôle individuel</h3><div class='row'>";

  for (int i = 0; i < FAN_COUNT; i++) {
    html += "<div class='col'><div class='card'>";
    html += "<h4>Ventilateur " + String(i + 1) + "</h4>";
    html += "<p>État : ";
    html += fanEnabled[i] ? "<span class='badge badge-on'>ON</span>" : "<span class='badge badge-off'>OFF</span>";
    html += " <a class='btn-link' href='/toggleFan?fan=" + String(i) + "'>";
    html += fanEnabled[i] ? "Éteindre" : "Allumer";
    html += "</a></p>";

    html += "<label>Couleur :</label><br>";
    html += "<input type='color' id='c" + String(i) + "'>";
    html += "<button type='button' onclick='setColor(" + String(i) + ")'>Appliquer</button><br><br>";

    html += "<label>Mode :</label><br>";
    html += "<a class='btn-link' href='/setMode?fan=" + String(i) + "&m=1'>Fixe</a>";
    html += "<a class='btn-link' href='/setMode?fan=" + String(i) + "&m=2'>Pulser</a>";
    html += "<a class='btn-link' href='/setMode?fan=" + String(i) + "&m=3'>Pulser+Couleur</a>";
    html += "<a class='btn-link' href='/setMode?fan=" + String(i) + "&m=4'>Cycle</a>";
    html += "<a class='btn-link' href='/setMode?fan=" + String(i) + "&m=5'>Timer</a>";
    html += "<a class='btn-link' href='/setMode?fan=" + String(i) + "&m=0'>OFF</a>";

    html += "</div></div>";
  }

  html += "</div></div>";

  // JS
  html += R"(
<script>
function setColor(f){
  var c = document.getElementById("c"+f).value.substring(1);
  fetch("/setColor?fan="+f+"&c="+c);
}
function setGlobalColor(){
  var c = document.getElementById("gcolor").value.substring(1);
  fetch("/setGlobalColor?c="+c);
}
</script>
</div></body></html>
)";
  return html;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < FAN_COUNT; i++) {
    fans[i] = new Adafruit_NeoPixel(fanLeds[i], fanPins[i], NEO_GRB + NEO_KHZ800);
    fans[i]->begin();
    fans[i]->show();
    modeFan[i] = MODE_STATIC;
    R[i] = 255; G[i] = 0; B[i] = 0;
  }

  WiFi.begin(ssid, password);
  Serial.print("Connexion");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnecté, IP: " + WiFi.localIP().toString());

  server.on("/", []() {
    server.send(200, "text/html", htmlPage());
  });

  server.on("/setTime", []() {
    currentHour   = server.arg("h").toInt();
    currentMinute = server.arg("m").toInt();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/setWindow", []() {
    startHour   = server.arg("sh").toInt();
    startMinute = server.arg("sm").toInt();
    stopHour    = server.arg("eh").toInt();
    stopMinute  = server.arg("em").toInt();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/toggleGlobal", []() {
    globalControl = server.arg("on").toInt() == 1;
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/toggleAll", []() {
    globalEnabled = server.arg("on").toInt() == 1;
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/setGlobalColor", []() {
    String hex = server.arg("c");
    long num = strtol(hex.c_str(), NULL, 16);
    gR = num >> 16;
    gG = (num >> 8) & 0xFF;
    gB = num & 0xFF;
    server.send(200, "text/plain", "OK");
  });

  server.on("/setGlobalMode", []() {
    globalMode = server.arg("m").toInt();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/setSpeeds", []() {
    pulseSpeed      = constrain(server.arg("ps").toInt(), 1, 10);
    pulseColorSpeed = constrain(server.arg("pcs").toInt(), 1, 10);
    cycleSpeed      = constrain(server.arg("cs").toInt(), 1, 10);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/toggleFan", []() {
    int f = server.arg("fan").toInt();
    if (f >= 0 && f < FAN_COUNT) {
      fanEnabled[f] = !fanEnabled[f];
      if (!fanEnabled[f]) setFanColor(f, 0, 0, 0);
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/setMode", []() {
    int f = server.arg("fan").toInt();
    if (f >= 0 && f < FAN_COUNT) {
      modeFan[f] = server.arg("m").toInt();
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/setColor", []() {
    int f = server.arg("fan").toInt();
    String hex = server.arg("c");
    long num = strtol(hex.c_str(), NULL, 16);
    if (f >= 0 && f < FAN_COUNT) {
      R[f] = num >> 16;
      G[f] = (num >> 8) & 0xFF;
      B[f] = num & 0xFF;
      modeFan[f] = MODE_STATIC;
    }
    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

// ===== LOOP =====
void loop() {
  server.handleClient();
  updateClock();

  if (millis() - lastAnim < 20) return;
  lastAnim = millis();

  int pulseStep      = map(pulseSpeed,      1, 10, 1, 10);
  int pulseColorStep = map(pulseColorSpeed, 1, 10, 1, 10);
  int cycleStep      = map(cycleSpeed,      1, 10, 1, 5);

  if (pulseUp) pulsePhase += pulseStep;
  else         pulsePhase -= pulseStep;
  if (pulsePhase >= 255) { pulsePhase = 255; pulseUp = false; }
  if (pulsePhase <= 0)   { pulsePhase = 0;   pulseUp = true;  }

  cyclePos = (cyclePos + cycleStep) % 256;

  bool inWindow = timeInRange(currentHour, currentMinute,
                              startHour, startMinute,
                              stopHour, stopMinute);

  for (int f = 0; f < FAN_COUNT; f++) {
    if (!globalControl) {
      // ----- MODE INDIVIDUEL -----
      if (!fanEnabled[f]) {
        setFanColor(f, 0, 0, 0);
        continue;
      }

      switch (modeFan[f]) {
        case MODE_OFF:
          setFanColor(f, 0, 0, 0);
          break;

        case MODE_STATIC:
          setFanColor(f, R[f], G[f], B[f]);
          break;

        case MODE_PULSE: {
          uint8_t r = (R[f] * pulsePhase) / 255;
          uint8_t g = (G[f] * pulsePhase) / 255;
          uint8_t b = (B[f] * pulsePhase) / 255;
          setFanColor(f, r, g, b);
        } break;

        case MODE_PULSE_COLOR: {
          uint32_t col = wheel((pulsePhase * pulseColorStep + f * 20) & 255);
          uint8_t r = col >> 16;
          uint8_t g = (col >> 8) & 0xFF;
          uint8_t b = col & 0xFF;
          setFanColor(f, r, g, b);
        } break;

        case MODE_COLOR_CYCLE: {
          for (int i = 0; i < fanLeds[f]; i++) {
            fans[f]->setPixelColor(i, wheel((i + cyclePos) & 255));
          }
          fans[f]->show();
        } break;

        case MODE_TIMER:
          if (inWindow) setFanColor(f, R[f], G[f], B[f]);
          else          setFanColor(f, 0, 0, 0);
          break;
      }

    } else {
      // ----- MODE GLOBAL -----
      if (!globalEnabled) {
        setFanColor(f, 0, 0, 0);
        continue;
      }

      switch (globalMode) {
        case MODE_OFF:
          setFanColor(f, 0, 0, 0);
          break;

        case MODE_STATIC:
          setFanColor(f, gR, gG, gB);
          break;

        case MODE_PULSE: {
          uint8_t r = (gR * pulsePhase) / 255;
          uint8_t g = (gG * pulsePhase) / 255;
          uint8_t b = (gB * pulsePhase) / 255;
          setFanColor(f, r, g, b);
        } break;

        case MODE_PULSE_COLOR: {
          uint32_t col = wheel((pulsePhase * pulseColorStep) & 255);
          uint8_t r = col >> 16;
          uint8_t g = (col >> 8) & 0xFF;
          uint8_t b = col & 0xFF;
          setFanColor(f, r, g, b);
        } break;

        case MODE_COLOR_CYCLE: {
          for (int i = 0; i < fanLeds[f]; i++) {
            fans[f]->setPixelColor(i, wheel((i + cyclePos) & 255));
          }
          fans[f]->show();
        } break;

        case MODE_TIMER:
          if (inWindow) setFanColor(f, gR, gG, gB);
          else          setFanColor(f, 0, 0, 0);
          break;
      }
    }
  }
}
