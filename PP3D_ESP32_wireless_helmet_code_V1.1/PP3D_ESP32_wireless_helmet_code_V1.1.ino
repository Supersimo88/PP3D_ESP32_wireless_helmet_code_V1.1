
#include <WiFi.h>
#include <esp_now.h>
#include <ESP32Servo.h>

// ---------------- PIN DEFINITIONS ----------------
#define SERVO_1_PIN 1
#define SERVO_2_PIN 2
#define SERVO_3_PIN 8
#define LED1_PIN 4
#define LED2_PIN 5
#define BUTTON_PIN 6

// ---------------- GLOBAL OBJECTS -----------------
Servo servo1;
Servo servo2;
Servo servo3;

// Replace this with the real MAC of the sound board
uint8_t soundBoardMAC[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// ---------------- SERVO CONFIG -------------------
int servo1Close = 0;
int servo2Close = 180;
int servo3Close = 0;

int servo1Open = 180;
int servo2Open = 0;
int servo3Open = 180;

enum Position { OPEN_POSITION, CLOSE_POSITION };
Position currentPosition1 = CLOSE_POSITION;
Position currentPosition2 = CLOSE_POSITION;
Position currentPosition3 = CLOSE_POSITION;

int openSpeed = 10;
int closeSpeed = 10;

bool detachServo1AtEnd = true;
bool detachServo2AtEnd = true;
bool detachServo3AtEnd = true;

unsigned long lastServoUpdate = 0;
const unsigned long servoUpdateInterval = 20;

int servo1Target = 0;
int servo2Target = 180;
int servo3Target = 180;
int servo1Position = 0;
int servo2Position = 180;
int servo3Position = 180;

unsigned long detachTime1 = 0;
unsigned long detachTime2 = 0;
unsigned long detachTime3 = 0;

// ---------------- BUTTON ----------------
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 500;

// ---------------- LED CONTROL ----------------
unsigned long ledCloseDelayTime = 700;
unsigned long ledCloseDelayStart = 0;
bool ledsOn = false;

// ---------------- FLICKER EFFECT ----------------
bool enableFlickerEffect = true;
int flickerTotalCount = 3;
int flickerInterval = 35;
int remainingFlickers = 0;

unsigned long flickerStartTime = 0;
bool flickering = false;
int flickerState = 0;

// Wireless receive timestamp
unsigned long lastDataReceive = 0;

// ---------------- FUNCTION DECLARATIONS ----------------
void onDataReceive(const esp_now_recv_info *info, const uint8_t *incomingData, int len);
void sendSoundEffect(uint8_t effect);
void updateServoPositions();
void attachServos();
void toggleHelmetState();
void initializeClosedPosition();
void turnOffLEDs();
void startFlickerEffect();
void executeFlickerEffect();

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== IRON MAN HELMET CONTROLLER START ===");

  // Display MAC using Arduino method (esp-idf 5.x removed esp_read_mac)
  Serial.print("ESP32 MAC (STA): ");
  Serial.println(WiFi.macAddress());

  servo1.attach(SERVO_1_PIN);
  servo2.attach(SERVO_2_PIN);
  servo2.attach(SERVO_3_PIN);

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  Serial.println("[WiFi] Station mode enabled.");

  // ---------------- ESP-NOW INIT ----------------
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Initialization FAILED!");
    return;
  }
  Serial.println("[ESP-NOW] Initialized successfully.");

  // Register callback (new required signature)
  esp_now_register_recv_cb(onDataReceive);

  // Add peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, soundBoardMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("[ESP-NOW] Peer added successfully.");
  } else {
    Serial.println("[ESP-NOW] Peer ADD FAILED!");
  }

  initializeClosedPosition();
}

// ---------------- MAIN LOOP ----------------
void loop() {
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    Serial.println("[BUTTON] Press detected.");
    toggleHelmetState();
  }

  updateServoPositions();

  if (currentPosition1 == CLOSE_POSITION && millis() - ledCloseDelayStart >= ledCloseDelayTime && !ledsOn) {
    Serial.println("[LED] Delay finished → turning LEDs ON");
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    ledsOn = true;

    if (enableFlickerEffect) startFlickerEffect();
  }

  if (flickering) executeFlickerEffect();

  if (currentPosition1 == OPEN_POSITION && ledsOn) {
    Serial.println("[LED] Helmet open → LEDs OFF");
    turnOffLEDs();
  }
}

// ---------------- INITIAL POSITION ----------------
void initializeClosedPosition() {
  servo1Position = servo1Close;
  servo2Position = servo2Close;
  servo3Position = servo3Close;

  servo1.write(servo1Position);
  servo2.write(servo2Position);
  servo3.write(servo2Position);

  currentPosition1 = CLOSE_POSITION;
  currentPosition2 = CLOSE_POSITION;
  currentPosition3 = CLOSE_POSITION;

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  ledCloseDelayStart = millis();

  if (enableFlickerEffect) startFlickerEffect();
}

// ---------------- TOGGLE ----------------
void toggleHelmetState() {
  detachTime1 = detachTime2 = detachTime3 = 0;

  if (currentPosition1 == CLOSE_POSITION) {
    Serial.println("[HELMET] Opening...");
    sendSoundEffect(2);

    servo1Target = servo1Open;
    servo2Target = servo2Open;
    servo3Target = servo3Open;

    currentPosition1 = OPEN_POSITION;
    currentPosition2 = OPEN_POSITION;
    currentPosition3 = OPEN_POSITION;

    attachServos();
  } else {
    Serial.println("[HELMET] Closing...");
    sendSoundEffect(3);

    ledsOn = false;
    turnOffLEDs();

    servo1Target = servo1Close;
    servo2Target = servo2Close;
    servo3Target = servo3Close;

    currentPosition1 = CLOSE_POSITION;
    currentPosition2 = CLOSE_POSITION;
    currentPosition3 = CLOSE_POSITION;

    ledCloseDelayStart = millis();

    attachServos();
  }
}

// ---------------- SERVO UPDATE ----------------
void updateServoPositions() {
  unsigned long currentTime = millis();
  if (currentTime - lastServoUpdate < servoUpdateInterval) return;

  lastServoUpdate = currentTime;

  int speed1 = (currentPosition1 == OPEN_POSITION) ? openSpeed : closeSpeed;
  int speed2 = (currentPosition2 == OPEN_POSITION) ? openSpeed : closeSpeed;
  int speed3 = (currentPosition3 == OPEN_POSITION) ? openSpeed : closeSpeed;

  if (servo1Position != servo1Target) {
    servo1Position += (servo1Position < servo1Target) ? speed1 : -speed1;
    servo1Position = constrain(servo1Position, 0, 180);
    servo1.write(servo1Position);
  }

  if (servo2Position != servo2Target) {
    servo2Position += (servo2Position < servo2Target) ? speed2 : -speed2;
    servo2Position = constrain(servo2Position, 0, 180);
    servo2.write(servo2Position);
  }

  if (servo3Position != servo3Target) {
    servo3Position += (servo3Position < servo3Target) ? speed3 : -speed3;
    servo3Position = constrain(servo3Position, 0, 180);
    servo3.write(servo3Position);
  }
}

// ---------------- ATTACH SERVOS ----------------
void attachServos() {
  if (!servo1.attached()) servo1.attach(SERVO_1_PIN);
  if (!servo2.attached()) servo2.attach(SERVO_2_PIN);
  if (!servo3.attached()) servo3.attach(SERVO_3_PIN);
}

// ---------------- SEND SOUND EFFECT ----------------
void sendSoundEffect(uint8_t effect) {
  esp_err_t result = esp_now_send(soundBoardMAC, &effect, 1);
  Serial.print("[ESP-NOW] Sending sound effect ");
  Serial.print(effect);
  Serial.print(" → ");

  if (result == ESP_OK) Serial.println("OK");
  else Serial.println("FAILED");
}

// ---------------- NEW ESP-NOW CALLBACK ----------------
void onDataReceive(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len <= 0) return;

  Serial.print("[ESP-NOW RX] From: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", info->src_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" | Data: ");
  Serial.println(incomingData[0]);

  if (incomingData[0] == 101) toggleHelmetState();
}

// ---------------- FLICKER CONTROL ----------------
void startFlickerEffect() {
  flickering = true;
  remainingFlickers = flickerTotalCount;
  flickerStartTime = millis();
}

void executeFlickerEffect() {
  unsigned long now = millis();
  if (now - flickerStartTime < flickerInterval) return;

  flickerStartTime = now;

  flickerState ^= 1;
  digitalWrite(LED1_PIN, flickerState);
  digitalWrite(LED2_PIN, flickerState);

  if (flickerState == 0) {
    remainingFlickers--;
    if (remainingFlickers == 0) {
      flickering = false;
      digitalWrite(LED1_PIN, HIGH);
      digitalWrite(LED2_PIN, HIGH);
    }
  }
}

// ---------------- LED OFF ----------------
void turnOffLEDs() {
  flickering = false;
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  ledsOn = false;
}
