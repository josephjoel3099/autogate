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

#define BAUD_RATE 115200
#define LIMIT_SWITCH_THRESHOLD 800
#define GATE_TIMEOUT 40000  // 40 seconds timeout for gate operation

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
  gateOperationStartTime = millis();
  gateState = GATE_CLOSING;
  Serial.println("GATE_CLOSING");
}

void stopGate() {
  digitalWrite(MOTOR_A, LOW);
  digitalWrite(MOTOR_B, LOW);
  gateState = GATE_STOPPED;
  Serial.println("GATE_STOPPED");
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
void setupWiFi() {
  WiFi.begin(SSID, PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

/* ---------- SETUP ---------- */
void setup() {
  Serial.begin(BAUD_RATE);

  pinMode(MOTOR_A, OUTPUT);
  pinMode(MOTOR_B, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LEFT_SWITCH, INPUT);
  pinMode(RIGHT_SWITCH, INPUT);

  stopGate();
  setupWiFi();

  gate.onRangeValue(onRangeValue);

  SinricPro.begin(APP_KEY, APP_SECRET);

  //Wait for sinricpro to connect
  Serial.print("Connecting SinricPro");
  while (!SinricPro.isConnected()) {
    SinricPro.handle();
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nSinricPro connected");

  // Initial door state based on limit switches
  if (digitalRead(LEFT_SWITCH) == HIGH) {
    gate.sendRangeValueEvent(100); // OPENED (100%)
    Serial.println("Initial state: Gate OPENED (100%)");
  } else if (digitalRead(RIGHT_SWITCH) == HIGH) {
    gate.sendRangeValueEvent(0);  // CLOSED (0%)
    Serial.println("Initial state: Gate CLOSED (0%)");
  } else {
    Serial.println("Initial state: Gate UNKNOWN");
  }
}

/* ---------- LOOP ---------- */
void loop() {
  SinricPro.handle();

  // Blink built-in LED to show ESP is running
  static unsigned long lastBlinkTime = 0;
  static bool ledState = false;
  if (millis() - lastBlinkTime >= 1000) {
    lastBlinkTime = millis();
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH); // LED_BUILTIN is active LOW on ESP8266
  }

  int leftReading = digitalRead(LEFT_SWITCH);
  int rightReading = digitalRead(RIGHT_SWITCH);
  
  bool leftTriggered  = leftReading == HIGH;
  bool rightTriggered = rightReading == HIGH;

  // Stop when limits reached
  if (gateState == GATE_OPENING && leftTriggered) {
    stopGate();
    gate.sendRangeValueEvent(100); // OPENED (100%)
    Serial.println("Gate opened - limit switch reached (100%)");
  }

  if (gateState == GATE_CLOSING && rightTriggered) {
    stopGate();
    gate.sendRangeValueEvent(0);  // CLOSED (0%)
    Serial.println("Gate closed - limit switch reached (0%)");
  }

  // Check for timeout during gate operation
  if ((gateState == GATE_OPENING || gateState == GATE_CLOSING) && 
      (millis() - gateOperationStartTime >= GATE_TIMEOUT)) {
    stopGate();
    Serial.println("Gate stopped - timeout reached!");
  }
}
