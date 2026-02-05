//||======================||
//||--------EVOBOT--------||
//||======================||

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "driver/i2s.h"
#include "driver/ledc.h"
#include <Wire.h>
#include "DFRobotDFPlayerMini.h"
#include <Adafruit_INA219.h>

// ===== RASPI USB SERIAL =====
String raspiUSBBuffer = "";
String raspiColor = "";
String raspiDirection = "";
String gassString = "";
bool raspiDataReady = true;   // USB selalu aktif

//===== DFPlayer =====
DFRobotDFPlayerMini mp3;
//===== INA219 =====
Adafruit_INA219 ina219;

//===== TCA CONFIG =====
#define TCA_ADDR 0x70
#define INA_CHANNEL 3

//===== VOLTAGE READING CALIBRATION =====
#define V_GAIN   1.0084
#define V_OFFSET -0.0398

//===== UUID BLE UART =====
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// --- MOTOR 1 ---
#define M1_IN1 25
#define M1_IN2 33

// --- MOTOR 2 ---
#define M2_IN1 26
#define M2_IN2 27

// --- MOTOR 3 ---
#define M3_IN1 14
#define M3_IN2 12

// --- MOTOR 4 ---
#define M4_IN1 32
#define M4_IN2 23

//===== Ultrasonic =====
#define TRIG 5
#define ECHO 4

int PWM_SPEED = 255;   // default speed
bool speechControlMode = false;  // ??
bool voiceCommandMode = true;  // ??

BLECharacteristic *txCharacteristic;
String lastCommand = "S";
String received;

long lastDistance = 0;
unsigned long lastPing = 0;
bool deviceConnected = false;
unsigned long intervalRead = 0;
long lastsearching = 0;
unsigned long duration = 0;
bool greenMove = false;
bool gassSensor = false;
//float busVoltage[5] = {11.48, 6.45, 0.78, 3.14, 9.01};
//uint8_t step = 0;
long voiceduration = 0;
enum RobotMode {
  MODE_MANUAL,
  MODE_COLOR1,
  MODE_COLOR2,
  MODE_COLOR3,
  MODE_COLOR4,
  MODE_OBSTACLE,
  MODE_AUTO,
  MODE_VOICE
};
RobotMode currentMode = MODE_MANUAL;

//===== PWM setup for ESP32 =====
#define PWM_FREQ 1000
#define PWM_RES 8

// assign PWM channels
int CH_M1_IN1 = 0;
int CH_M1_IN2 = 1;
int CH_M2_IN1 = 2;
int CH_M2_IN2 = 3;
int CH_M3_IN1 = 4;
int CH_M3_IN2 = 5;
int CH_M4_IN1 = 6;
int CH_M4_IN2 = 7;

//===== Forward Declarations =====
void allForward();
void allBackward();
void left();
void right();
void stopAllMotors();
void slow();
long readDistance();

//===== BLE Callbacks =====
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Serial.println("Device connected");
      deviceConnected = true;
    }

    void onDisconnect(BLEServer* pServer) {
      Serial.println("Device disconnected");
      deviceConnected = false;
      BLEDevice::startAdvertising();
      stopAllMotors();
    }
};

class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) {
    if (deviceConnected) {
      received = characteristic->getValue().c_str();
      received.trim();
      Serial.println(received);
    }
  }
};

//===== SETUP =====
void setup() {
  Serial.begin(115200);

  //===== BLE =====
  BLEDevice::init("EVOBOT");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *rxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  rxCharacteristic->setCallbacks(new RxCallback());

  txCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::getAdvertising()->start();

  //===== SETUP DFPLAYER =====
  Serial2.begin(9600, SERIAL_8N1, 16, 17); // RX, TX pins
  delay(500);
  Serial.println("Starting DFPlayer...");
  if (!mp3.begin(Serial2)) {
    Serial.println("DFPlayer initialization failed!");
  }
  mp3.volume(29); // volume : 0 - 30
  mp3.playMp3Folder(7);  // abang saleh dorong mobil
  Serial.println("DFPlayer ready...");
  
  //===== SETUP INA219 =====
  Wire.begin(21, 22);
  tcaSelect(INA_CHANNEL);

  if (!ina219.begin()) {
    Serial.println("Failed to find INA219!");
  }
  ina219.setCalibration_32V_2A();
  Serial.println("INA219 ready...");

  //=====Setup camMode=====
  Serial.println("START");

  //===== SETUP PWM =====
  int channels[] = {CH_M1_IN1, CH_M1_IN2, CH_M2_IN1, CH_M2_IN2, CH_M3_IN1, CH_M3_IN2, CH_M4_IN1, CH_M4_IN2};
  int pins[]     = {M1_IN1, M1_IN2, M2_IN1, M2_IN2, M3_IN1, M3_IN2, M4_IN1, M4_IN2};

  for (int i = 0; i < 8; i++) {
    ledcSetup(channels[i], PWM_FREQ, PWM_RES);
    ledcAttachPin(pins[i], channels[i]);
  }
  stopAllMotors();

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
}


//===== LOOP =====
void loop() {
  receivedfromapp();
  readRaspiUSB();
  gassHandler();
  switch (currentMode) {
    case MODE_MANUAL:
      joystickMode();
      break;

    case MODE_COLOR1:
      greenForward();
      break;

    case MODE_COLOR2:
      yellowForward();
      break;

    case MODE_COLOR3:
      redForward();
      break;

    case MODE_COLOR4:
      searchingGreen();
      break;

    case MODE_OBSTACLE:
      obstacleMode();
      break;

    case MODE_AUTO:
      autoMode();
      break;
    
    case MODE_VOICE:
      voiceMode();
      break;
  }
  if (millis() - intervalRead >= 1000) {
    batterryMonitoring();
    intervalRead = millis();
  }
}

void receivedfromapp() {
  if (received.length() > 0) {
    if (received != "VOICECOMMANDON" && received != "VOICECOMMANDOFF" && received.startsWith("V")) {
      int newSpeed = received.substring(1).toInt();
      PWM_SPEED = constrain(newSpeed * 2.55, 60, 255);
    }

    else if (received == "YisOFF") {
      setMode(MODE_MANUAL);
      Serial.println("STOPDETECTION");
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
    }

    else if (received == "A") gassSensor = true;
    else if (received == "AisOFF") gassSensor = false;

    else if (received == "VOICECOMMANDON") {
      duration = millis();
      Serial.println("VoiceON");
      setMode(MODE_VOICE);
    }
    else if (received == "VOICECOMMANDOFF") {
      duration = millis();
      Serial.println("VoiceOFF");
      setMode(MODE_MANUAL);
    }

    else if (received == "XisOFF") {
      setMode(MODE_MANUAL);
      Serial.println("STOPDETECTION");
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
    }

    else if (
      received == "F" ||
      received == "B" ||
      received == "L" ||
      received == "R" ||
      received == "S"
    ) {
      lastCommand = received;
    }

    // ======= DFPLAYER AUDIO =======
    else if (received == "001") mp3.playMp3Folder(1);  // perkenalan
    else if (received == "002") mp3.playMp3Folder(2);  // fitur
    else if (received == "003") mp3.playMp3Folder(3);  // ruby-chan
    else if (received == "004") mp3.playMp3Folder(4);  // lagu
    else if (received == "BisOFF") mp3.stop();

    // ======= AUTONOMOUS =======
    else if (received == "011") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("GREENFORWARD");
      setMode(MODE_COLOR1);
    }
    else if (received == "012") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("YELLOWFORWARD");
      setMode(MODE_COLOR2);
    }
    else if (received == "013") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("REDFORWARD");
      setMode(MODE_COLOR3);
    }
    else if (received == "014") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_U");
      txCharacteristic->notify();
      Serial.println("GREENSEARCHING");
      setMode(MODE_COLOR4);
    }
    else if (received == "021") {
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
      Serial.println("STOPDETECTION");
      setMode(MODE_OBSTACLE);
    }
    else if (received == "022") {
      duration = millis();
      txCharacteristic->setValue("SELESAI_C");
      txCharacteristic->notify();
      Serial.println("STOPDETECTION");
      setMode(MODE_AUTO);
    }
    received = "";
  }
  
}

void joystickMode() {
  if (lastCommand == "F") allForward();
  else if (lastCommand == "B") allBackward();
  else if (lastCommand == "L") left();
  else if (lastCommand == "R") right();
  else if (lastCommand == "S") stopAllMotors();
}

void greenForward() {
  if (raspiDirection == "STOP") {
    stopAllMotors();
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
  if (raspiColor == "GREEN") {
    if (raspiDirection == "FORWARD") allForward();
    else if (raspiDirection == "LEFT") left();
    else if (raspiDirection == "RIGHT") right();
    else if (raspiDirection == "SLOW") slow();
  }
  else if (raspiDirection == "STOPMOMENTARY") stopAllMotors();
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
}

void yellowForward() {
  if (raspiDirection == "STOP") {
    stopAllMotors();
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
  else if (raspiDirection == "STOPMOMENTARY") stopAllMotors();
  if (raspiColor == "YELLOW") {
    if (raspiDirection == "FORWARD") allForward();
    else if (raspiDirection == "LEFT") left();
    else if (raspiDirection == "RIGHT") right();
    else if (raspiDirection == "SLOW") slow();
  }
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
}

void redForward() {
  if (raspiDirection == "STOP") {
    stopAllMotors();
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
  else if (raspiDirection == "STOPMOMENTARY") stopAllMotors();
  if (raspiColor == "RED") {
    if (raspiDirection == "FORWARD") allForward();
    else if (raspiDirection == "LEFT") left();
    else if (raspiDirection == "RIGHT") right();
    else if (raspiDirection == "SLOW") slow();
  }
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
}

void searchingGreen() {
  if (millis() - lastsearching >= 3000) {
    lastsearching = millis();
    greenMove = !greenMove;
  }
  if (greenMove == 0) allForward();
  else if (greenMove == 1) right();

  if (raspiColor == "GREEN") {
    stopAllMotors();
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_C");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
}

void autoMode() {
  
  //Serial.println(distance);
  if (millis() - lastPing >= 20) {
    long distance = readDistance();
    if (distance > 0) {
      lastDistance = distance;
      lastPing = millis();
    }
  }  
  if (lastDistance > 30) allForward();
  else right();
  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_U");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
    Serial.println("STOPDETECTION");
    return;
  }
}

void obstacleMode() {
  //Serial.println(lastDistance);
  if (lastDistance <= 30 &&
  lastCommand != "B" &&
  lastCommand != "L" &&
  lastCommand != "R") {
    stopAllMotors();
  }
  if (millis() - lastPing >= 20) {
    long distance = readDistance();
    if (distance > 0) {
      lastDistance = distance;
      lastPing = millis();
    }
  }  
  if (lastCommand == "F") {
    if (lastDistance > 30) allForward();
    else if (lastDistance <= 30) stopAllMotors();
  }
  else if (lastCommand == "B") allBackward();
  else if (lastCommand == "L") left();
  else if (lastCommand == "R") right();
  else if (lastCommand == "S") stopAllMotors();
}

void voiceMode() {
  if (raspiDirection == "MAJU") allForward();
  else if (raspiDirection == "MUNDUR") allBackward();
  else if (raspiDirection == "PUTARKANAN") right();
  else if (raspiDirection == "PUTARKIRI") left();
  else if (raspiDirection == "MAJUPELAN") stopAllMotors();
  else if (raspiDirection == "BERHENTI") stopAllMotors();

  if (millis() - duration >= 60000) {
    txCharacteristic->setValue("SELESAI_V");
    txCharacteristic->notify();
    setMode(MODE_MANUAL);
  }
}

void gassHandler() {
  if (gassSensor) {
    if (gassString == "DETECTED") {
      mp3.playMp3Folder(5);  // abang saleh teriak api
      gassString = "";
    }
    else if (gassString == "SAFE") {
      mp3.playMp3Folder(6);  // abang saleh ngaduk dodol
      gassString = "";
    }
  }
}

// ================= BATTERRY LEVEL READING =================
void tcaSelect(uint8_t tca_ch) {
  if (tca_ch > 7) return;
  
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << tca_ch);
  Wire.endTransmission();
}

void batterryMonitoring() {
  tcaSelect(INA_CHANNEL);
  float busVoltage = ina219.getBusVoltage_V();
  // calibrate the voltage
  float calibratedVoltage = (busVoltage * V_GAIN) + V_OFFSET;
  //float calibratedVoltage = (busVoltage[step] * V_GAIN) + V_OFFSET; // TESTING MODE
  // map into a percentage
  float batterryPercentage = fmap(calibratedVoltage, 0.0, 12.0, 0.0, 100.0); // send this to app for batterry monitoring

  String msg = "BAT:" + String(batterryPercentage, 1);
  txCharacteristic->setValue(msg.c_str());
  txCharacteristic->notify();

  /*Serial.println("===== BATTERRY MONITORING =====");
  Serial.print("Voltage : ");
  Serial.print(calibratedVoltage, 3);
  Serial.println(" V");
  Serial.print("Percentage : ");
  Serial.print(batterryPercentage);
  Serial.println(" %");
  Serial.println();*/

  /*// ===== TESTING MODE =====
  if (step < 4) step++;
  else step = 0;*/
}
// ========= MAP TO PERCENTAGE =========
float fmap(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void readRaspiUSB() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') {
      parseRaspiData(raspiUSBBuffer);
      raspiUSBBuffer = "";
    } else {
      raspiUSBBuffer += c;
    }
  }
}


void parseRaspiData(String data) {
  data.trim();
  if (gassSensor) gassString == data;
  if (data == "VoiceOFF") {
    txCharacteristic->setValue("nonBlock");
    txCharacteristic->notify();
  }

  int commaIndex = data.indexOf(',');
  if (commaIndex == -1) return; 

  raspiColor = data.substring(0, commaIndex);
  raspiDirection = data.substring(commaIndex + 1);

  raspiColor.trim();
  raspiDirection.trim();

  raspiDataReady = true;
}

void setMode(RobotMode m) {
  stopAllMotors();
  currentMode = m;
  raspiColor = "";
  raspiDirection = "";
}

//================= MOTOR FUNCTIONS =================
//===== MAJU =====
void allForward() {
  ledcWrite(CH_M1_IN1, PWM_SPEED); ledcWrite(CH_M1_IN2, 0);
  ledcWrite(CH_M2_IN1, PWM_SPEED); ledcWrite(CH_M2_IN2, 0);
  ledcWrite(CH_M3_IN1, PWM_SPEED); ledcWrite(CH_M3_IN2, 0);
  ledcWrite(CH_M4_IN1, PWM_SPEED); ledcWrite(CH_M4_IN2, 0);
}

//===== MUNDUR =====
void allBackward() {
  ledcWrite(CH_M1_IN1, 0); ledcWrite(CH_M1_IN2, PWM_SPEED);
  ledcWrite(CH_M2_IN1, 0); ledcWrite(CH_M2_IN2, PWM_SPEED);
  ledcWrite(CH_M3_IN1, 0); ledcWrite(CH_M3_IN2, PWM_SPEED);
  ledcWrite(CH_M4_IN1, 0); ledcWrite(CH_M4_IN2, PWM_SPEED);
}

//===== BELOK KANAN =====
void right() {
  ledcWrite(CH_M1_IN1, 0);         ledcWrite(CH_M1_IN2, PWM_SPEED);
  ledcWrite(CH_M4_IN1, PWM_SPEED); ledcWrite(CH_M4_IN2, 0);
  ledcWrite(CH_M2_IN1, PWM_SPEED); ledcWrite(CH_M2_IN2, 0);
  ledcWrite(CH_M3_IN1, 0);         ledcWrite(CH_M3_IN2, PWM_SPEED);
}

//===== BELOK KIRI =====
void left() {
  ledcWrite(CH_M1_IN1, PWM_SPEED); ledcWrite(CH_M1_IN2, 0);
  ledcWrite(CH_M3_IN1, PWM_SPEED); ledcWrite(CH_M3_IN2, 0);
  ledcWrite(CH_M2_IN1, 0);         ledcWrite(CH_M2_IN2, PWM_SPEED);
  ledcWrite(CH_M4_IN1, 0);         ledcWrite(CH_M4_IN2, PWM_SPEED);
  
}

//===== STOP =====
void stopAllMotors() {
  for (int ch = 0; ch <= 7; ch++) ledcWrite(ch, 0);
}

//===== SLOW (50%) =====
void slow() {
  int s = PWM_SPEED * 0.5;
  ledcWrite(CH_M1_IN1, s); ledcWrite(CH_M1_IN2, 0);
  ledcWrite(CH_M2_IN1, s); ledcWrite(CH_M2_IN2, 0);
  ledcWrite(CH_M3_IN1, s); ledcWrite(CH_M3_IN2, 0);
  ledcWrite(CH_M4_IN1, s); ledcWrite(CH_M4_IN2, 0);
}

void resetPWM() {
  int channels[] = {CH_M1_IN1, CH_M1_IN2, CH_M2_IN1, CH_M2_IN2, CH_M3_IN1, CH_M3_IN2, CH_M4_IN1, CH_M4_IN2};
  int pins[]     = {M1_IN1, M1_IN2, M2_IN1, M2_IN2, M3_IN1, M3_IN2, M4_IN1, M4_IN2};
  for (int i = 0; i < 8; i++) {
    ledcDetachPin(pins[i]);          // detach dulu
    ledcSetup(channels[i], PWM_FREQ, PWM_RES);
    ledcAttachPin(pins[i], channels[i]);
    ledcWrite(channels[i], 0);       // reset speed
  }
}

//===== Ultrasonic =====
long readDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long d = pulseIn(ECHO, HIGH, 12000);
  if (d == 0) return lastDistance;
  return d * 0.01715;
}