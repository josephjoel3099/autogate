#include <Arduino.h>

#ifdef ESP8266
  #include <ESP8266WiFi.h>
#endif

#include <SinricPro.h>
#include <SinricProGarageDoor.h>

/* ---------- SINRIC ---------- */
#define APP_KEY           "405a80c6-770f-4043-81ee-cf4ee6ca822d"      
#define APP_SECRET        "274e2c0d-9845-4089-8005-c7fca7c52139-7b121645-1b60-49fa-8bc6-e825521ef970"   
#define SWITCH_ID         "694be2bf6dbd335b28fa8c93"

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

/* ---------- DEVICE ---------- */
SinricProGarageDoor &gate = SinricPro[SWITCH_ID];

/* ---------- STATE ---------- */
enum GateState {
  GATE_STOPPED,
  GATE_OPENING,
  GATE_CLOSING
};

GateState gateState = GATE_STOPPED;

/* ---------- MOTOR CONTROL ---------- */
void openGate() {
  digitalWrite(MOTOR_A, LOW);
  digitalWrite(MOTOR_B, HIGH);
  gateState = GATE_OPENING;
  Serial.println("Gate GATE_OPENING");
}

void closeGate() {
  digitalWrite(MOTOR_A, HIGH);
  digitalWrite(MOTOR_B, LOW);
  gateState = GATE_CLOSING;
  Serial.println("Gate GATE_CLOSING");
}

void stopGate() {
  digitalWrite(MOTOR_A, LOW);
  digitalWrite(MOTOR_B, LOW);
  gateState = GATE_STOPPED;
  Serial.println("Gate GATE_STOPPED");
}

/* ---------- GARAGE DOOR CALLBACK ---------- */
bool onDoorState(const String &deviceId, bool &open) {
  if (open) {
    openGate();
  } else {
    closeGate();
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
  pinMode(LEFT_SWITCH, INPUT);
  pinMode(RIGHT_SWITCH, INPUT);

  stopGate();
  setupWiFi();

  gate.onDoorState(onDoorState);

  SinricPro.onConnected([] {
    Serial.println("SinricPro connected");
  });

  SinricPro.onDisconnected([] {
    Serial.println("SinricPro disconnected");
  });

  SinricPro.begin(APP_KEY, APP_SECRET);
}

/* ---------- LOOP ---------- */
void loop() {
  SinricPro.handle();

  int leftReading = digitalRead(LEFT_SWITCH);
  int rightReading = digitalRead(RIGHT_SWITCH);
  
  Serial.print("Left: ");
  Serial.print(leftReading);
  Serial.print(" | Right: ");
  Serial.println(rightReading);
  
  bool leftTriggered  = leftReading == HIGH;
  bool rightTriggered = rightReading == HIGH;

  // Stop when limits reached
  if (gateState == GATE_OPENING && leftTriggered) {
    stopGate();
    gate.sendDoorStateEvent(true);   // OPEN
  }

  if (gateState == GATE_CLOSING && rightTriggered) {
    stopGate();
    gate.sendDoorStateEvent(false);  // CLOSED
  }
}
