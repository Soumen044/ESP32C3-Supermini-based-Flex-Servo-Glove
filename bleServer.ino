#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <esp_log.h>
#include "esp_gap_ble_api.h"   // needed for ESP_BLE_SM_SET_STATIC_PASSKEY

#define SERVICE_UUID        "19B10000-E8F2-537E-4F6C-D104768A1214"
#define CHARACTERISTIC_UUID "19B10001-E8F2-537E-4F6C-D104768A1214"

// ---- Security ----
// Must match the CLIENT exactly. This is a pre-shared static passkey used for
// a real "Passkey Entry" MITM-protected pairing (not Just Works).
#define BLE_STATIC_PASSKEY 123456

const int FLEX_PIN_1 = 0;
const int FLEX_PIN_2 = 1;
const int FLEX_PIN_3 = 2;

const int OVERSAMPLE_COUNT = 16;
const float EMA_ALPHA = 0.20;

float emaFiltered1 = 0.0;
float emaFiltered2 = 0.0;
float emaFiltered3 = 0.0;

BLEServer* pServer = NULL;
BLECharacteristic* pChar = NULL;

bool clientConnected = false;
bool isBonded = false;
bool ledState = false;
unsigned long lastBlinkTime = 0;
uint32_t packetCount = 0;

struct SensorData {
  uint8_t angle1;
  uint8_t angle2;
  uint8_t angle3;
};

class ServerCB: public BLEServerCallbacks {
  void onConnect(BLEServer* p) override {
    clientConnected = true;
    Serial.println("\n[SERVER] ---> Client Connected! Requesting Security...");
  }
  void onDisconnect(BLEServer* p) override {
    clientConnected = false;
    isBonded = false;
    Serial.println("\n[SERVER] ---> Client Disconnected!");
    p->getAdvertising()->start();
  }
};

class SecurityCB : public BLESecurityCallbacks {
  // This device has "Display" capability (ESP_IO_CAP_OUT). With the static
  // passkey set, the stack calls this with our fixed passkey instead of a
  // random one -- there's nothing to "display" on real hardware, but we log
  // it for confirmation during bring-up.
  uint32_t onPassKeyRequest() override { return BLE_STATIC_PASSKEY; }
  void onPassKeyNotify(uint32_t k) override {
    Serial.printf("[SECURITY] ---> Passkey in use: %06u\n", k);
  }
  bool onConfirmPIN(uint32_t k) override { return true; }
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t res) override {
    if (res.success) {
      isBonded = true;
      Serial.println("[SECURITY] ---> BONDED & ENCRYPTED (MITM) SUCCESSFUL!");
    } else {
      isBonded = false;
      Serial.printf("[SECURITY] ---> BONDING FAILED! reason=%d\n", res.fail_reason);
    }
  }
};

int getAveragedADC(int pin) {
  long sum = 0;
  for (int i = 0; i < OVERSAMPLE_COUNT; i++) {
    sum += analogRead(pin);
    delayMicroseconds(100);
  }
  return (int)(sum / OVERSAMPLE_COUNT);
}

int getEMAFilteredADC(int pin, float &emaPrev) {
  int avgADC = getAveragedADC(pin);
  if (emaPrev == 0.0) emaPrev = avgADC;
  emaPrev = (EMA_ALPHA * avgADC) + ((1.0 - EMA_ALPHA) * emaPrev);
  return (int)emaPrev;
}

uint8_t getSensor1Details(int rawADC, String &state) {
  if (rawADC < 110) { state = "FULL BEND"; return 180; }
  else if (rawADC <= 599) { state = "SLIGHT BEND"; return 90; }
  else { state = "NORMAL"; return 0; }
}

uint8_t getSensor2Details(int rawADC, String &state) {
  if (rawADC < 250) { state = "FULL BEND"; return 180; }
  else if (rawADC <= 759) { state = "SLIGHT BEND"; return 90; }
  else { state = "NORMAL"; return 0; }
}

uint8_t getSensor3Details(int rawADC, String &state) {
  if (rawADC < 2300) { state = "FULL BEND"; return 180; }
  else if (rawADC <= 2449) { state = "SLIGHT BEND"; return 90; }
  else { state = "NORMAL"; return 0; }
}

// ---------------------------------------------------------------------------
// 3-MODE LED STATE MACHINE (called every loop, independent of BLE traffic)
//   Mode 1 - OFF          : no client connected
//   Mode 2 - BLINK BLUE   : client connected, securing/bonding in progress
//   Mode 3 - SOLID BLUE   : client connected AND bonded/encrypted (secure)
// ---------------------------------------------------------------------------
void updateServerLED() {
  if (!clientConnected) {
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    return;
  }

  if (isBonded) {
    neopixelWrite(RGB_BUILTIN, 0, 0, 64);
    return;
  }

  if (millis() - lastBlinkTime >= 200) {
    lastBlinkTime = millis();
    ledState = !ledState;
    neopixelWrite(RGB_BUILTIN, 0, 0, ledState ? 64 : 0);
  }
}

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);
  Serial.begin(115200);

  neopixelWrite(RGB_BUILTIN, 0, 0, 0); // LED OFF

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  BLEDevice::init("C3_SERVER");
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(new SecurityCB());

  // Force a fixed, pre-shared passkey instead of a randomly generated one.
  // Both server and client must set the SAME value.
  uint32_t staticPasskey = BLE_STATIC_PASSKEY;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &staticPasskey, sizeof(uint32_t));

  BLESecurity *pSec = new BLESecurity();
  pSec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND); // SC + MITM + Bonding
  pSec->setCapability(ESP_IO_CAP_OUT);                       // "Display" role
  pSec->setKeySize(16);
  pSec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  pSec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCB());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pChar = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  // Require encryption + MITM for client reading/notifying
  pChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
  pChar->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->start();

  Serial.println("=================================================");
  Serial.println("         BLE SERVER ONLINE (MITM SECURE)          ");
  Serial.println("=================================================");
}

void loop() {
  int clean1 = getEMAFilteredADC(FLEX_PIN_1, emaFiltered1);
  int clean2 = getEMAFilteredADC(FLEX_PIN_2, emaFiltered2);
  int clean3 = getEMAFilteredADC(FLEX_PIN_3, emaFiltered3);

  String state1, state2, state3;
  SensorData data;
  data.angle1 = getSensor1Details(clean1, state1);
  data.angle2 = getSensor2Details(clean2, state2);
  data.angle3 = getSensor3Details(clean3, state3);

  updateServerLED();

  if (clientConnected) {
    packetCount++;
    pChar->setValue((uint8_t*)&data, sizeof(SensorData));
    pChar->notify();

    Serial.printf("[TX #%04u] S1: %3d° | S2: %3d° | S3: %3d° | BONDED: %s\n",
                  packetCount, data.angle1, data.angle2, data.angle3, isBonded ? "YES" : "NO");
  } else {
    Serial.printf("[IDLE] S1: %4d | S2: %4d | S3: %4d | Waiting for Client...\n", clean1, clean2, clean3);
  }

  delay(100);
}
