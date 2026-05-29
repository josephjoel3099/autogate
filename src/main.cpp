#include <Arduino.h>

#ifdef ESP8266
  #include <ESP8266WiFi.h>
#endif

#include <SinricPro.h>
#include <SinricProBlinds.h>

/* ---------- SINRIC ---------- */
#define APP_KEY           "405a80c6-770f-4043-81ee-cf4ee6ca822d"
#define APP_SECRET        "274e2c0d-9845-4089-8005-c7fca7c52139-7b121645-1b60-49fa-8bc6-e825521ef970"
#define SWITCH_ID         "694fdb446dbd335b28fc4f8d"

/* ---------- WIFI ---------- */
#define SSID "Annmary Villa"
#define PASS "74740780"

/* ---------- PINS ---------- */
#define LEFT_SWITCH   D7
#define RIGHT_SWITCH  D6
#define MOTOR_A       D2
#define MOTOR_B       D3

/* ---------- TIMING ---------- */
#define BAUD_RATE             115200
#define GATE_TIMEOUT          40000   // 40s max gate travel time
#define WIFI_CONNECT_TIMEOUT  10000   // 10s per WiFi attempt
#define WIFI_CHECK_INTERVAL   5000    // check WiFi every 5s
#define LED_BLINK_INTERVAL    1000    // blink every 1s
#define LOOP_DELAY            10      // 10ms loop throttle (~100Hz)

/* ---------- DEVICE ---------- */
SinricProBlinds &gate = SinricPro[SWITCH_ID];

/* ---------- STATE ---------- */
enum GateState {
  GATE_STOPPED,
  GATE_OPENING,
  GATE_CLOSING
};

GateState gateState = GATE_STOPPED;
unsigned long gateOperationStartTime = 0;

/* ---------- MOTOR CONTROL ---------- */
void openGate() {
  digitalWrite(MOTOR_A, LOW);
  digitalWrite(MOTOR_B, HIGH);
  gateState = GATE_OPENING;
  gateOperationStartTime = millis();
  Serial.println("GATE_OPENING");
}

void closeGate() {
  digitalWrite(MOTOR_A, HIGH);
  digitalWrite(MOTOR_B, LOW);
  gateState = GATE_CLOSING;
  gateOperationStartTime = millis();
  Serial.println("GATE_CLOSING");
}

void stopGate() {
  digitalWrite(MOTOR_A, LOW);
  digitalWrite(MOTOR_B, LOW);
  gateState = GATE_STOPPED;
  Serial.println("GATE_STOPPED");
}

/* ---------- LIMIT SWITCH HANDLER ----------
   Called from both loop() and any blocking wait,
   so the gate is ALWAYS safe regardless of network state.
   -------------------------------------------------- */
void handleLimitSwitches() {
  bool leftTriggered  = (digitalRead(LEFT_SWITCH)  == HIGH);
  bool rightTriggered = (digitalRead(RIGHT_SWITCH) == HIGH);

  if (gateState == GATE_OPENING && leftTriggered) {
    stopGate();
    gate.sendRangeValueEvent(100);
    Serial.println("Gate opened - limit switch reached (100%)");
  }

  if (gateState == GATE_CLOSING && rightTriggered) {
    stopGate();
    gate.sendRangeValueEvent(0);
    Serial.println("Gate closed - limit switch reached (0%)");
  }

  // Timeout safety — stops motor if limit switch never triggers
  if ((gateState == GATE_OPENING || gateState == GATE_CLOSING) &&
      (millis() - gateOperationStartTime >= GATE_TIMEOUT)) {
    stopGate();
    Serial.println("Gate stopped - timeout reached!");
  }
}

/* ---------- BLINDS CALLBACK ---------- */
bool onRangeValue(const String &deviceId, int &position) {
  if (position == 0) {
    closeGate();
  } else if (position == 100) {
    openGate();
  }
  return true;
}

/* ---------- WIFI ---------- */
void setupWiFi(unsigned long timeout = WIFI_CONNECT_TIMEOUT) {
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("GateController"); // older ESP8266 API, try this if setHostname doesn't work
  WiFi.begin(SSID, PASS);
  Serial.print("Connecting WiFi");

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < timeout) {
    handleLimitSwitches();
    delay(100);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAILED. Status: " + String(WiFi.status()));
  }
}

/* ---------- SETUP ---------- */
void setup() {
  Serial.begin(BAUD_RATE);

  pinMode(MOTOR_A,      OUTPUT);
  pinMode(MOTOR_B,      OUTPUT);
  pinMode(LED_BUILTIN,  OUTPUT);
  pinMode(LEFT_SWITCH,  INPUT);
  pinMode(RIGHT_SWITCH, INPUT);

  stopGate();
  setupWiFi();

  gate.onRangeValue(onRangeValue);
  SinricPro.begin(APP_KEY, APP_SECRET);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connecting SinricPro");
    unsigned long sinricStart = millis();
    while (!SinricPro.isConnected() && millis() - sinricStart < 15000) {
      SinricPro.handle();
      handleLimitSwitches();
      delay(500);
      Serial.print(".");
    }

    if (SinricPro.isConnected()) {
      Serial.println("\nSinricPro connected");
    } else {
      Serial.println("\nSinricPro FAILED — loop will retry");
    }
  } else {
    Serial.println("Skipping SinricPro — no WiFi");
  }

  // Report initial gate position from limit switches
  if (digitalRead(LEFT_SWITCH) == HIGH) {
    gate.sendRangeValueEvent(100);
    Serial.println("Initial state: Gate OPENED (100%)");
  } else if (digitalRead(RIGHT_SWITCH) == HIGH) {
    gate.sendRangeValueEvent(0);
    Serial.println("Initial state: Gate CLOSED (0%)");
  } else {
    Serial.println("Initial state: Gate UNKNOWN");
  }
}

/* ---------- LOOP ---------- */
void loop() {
  SinricPro.handle();

  // --- Gate safety (highest priority) ---
  handleLimitSwitches();

  // --- LED heartbeat ---
  static unsigned long lastBlinkTime = 0;
  static bool ledState = false;
  if (millis() - lastBlinkTime >= LED_BLINK_INTERVAL) {
    lastBlinkTime = millis();
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH); // active LOW on ESP8266
  }

  // --- WiFi watchdog ---
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck >= WIFI_CHECK_INTERVAL) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost - attempting reconnect...");
      setupWiFi(); // non-blocking limit switch calls inside
      if (WiFi.status() == WL_CONNECTED && !SinricPro.isConnected()) {
        SinricPro.begin(APP_KEY, APP_SECRET);
        Serial.println("SinricPro reconnect triggered");
      }
    }
  }

  delay(LOOP_DELAY); // throttle to ~100Hz, reduces unnecessary CPU spin
}
