#include <M5Stack.h>
#include "DHT12.h"
#include <Wire.h>
#include "Adafruit_Sensor.h"
#include <Adafruit_BMP280.h>
#include "utility/MPU9250.h"
#include <MadgwickAHRS.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2904.h>

DHT12 dht12;
float dht12Temperature;
float dht12Humidity;

bool DHT12ReadData() {
  dht12Temperature = dht12.readTemperature();
  dht12Humidity    = dht12.readHumidity();
  return true;
};

Adafruit_BMP280 bme;
float bmePressure;

bool BMP280ReadData() {
  bmePressure = bme.readPressure(); 
  return true;
};

MPU9250 IMU;
Madgwick filter;

bool MPU9250ReadData() {
  if (!(IMU.readByte(MPU9250_ADDRESS, INT_STATUS) & 0x01)) {
    delay(0);
  }

  IMU.readAccelData(IMU.accelCount);
  IMU.getAres();
      
  IMU.ax = (float)IMU.accelCount[0]*IMU.aRes; // - accelBias[0];
  IMU.ay = (float)IMU.accelCount[1]*IMU.aRes; // - accelBias[1];
  IMU.az = (float)IMU.accelCount[2]*IMU.aRes; // - accelBias[2];

  IMU.readGyroData(IMU.gyroCount);  // Read the x/y/z adc values
  IMU.getGres();
     
  IMU.gx = (float)IMU.gyroCount[0]*IMU.gRes;
  IMU.gy = (float)IMU.gyroCount[1]*IMU.gRes;
  IMU.gz = (float)IMU.gyroCount[2]*IMU.gRes;

  IMU.readMagData(IMU.magCount);  // Read the x/y/z adc values
  IMU.getMres();
/*
  IMU.magbias[0] = +470.;
  IMU.magbias[1] = +120.;
  IMU.magbias[2] = +125.;
*/
  IMU.mx = (float)IMU.magCount[0]*IMU.mRes*IMU.magCalibration[0] - IMU.magbias[0];
  IMU.my = (float)IMU.magCount[1]*IMU.mRes*IMU.magCalibration[1] - IMU.magbias[1];
  IMU.mz = (float)IMU.magCount[2]*IMU.mRes*IMU.magCalibration[2] - IMU.magbias[2];

  filter.updateIMU(IMU.gx, IMU.gy, IMU.gz, IMU.ax, IMU.ay, IMU.az);
  IMU.roll = filter.getRoll();
  IMU.pitch = filter.getPitch();
  IMU.yaw = filter.getYaw();

  IMU.tempCount = IMU.readTempData();
  IMU.temperature = ((float) IMU.tempCount) / 333.87 + 21.0; 
    
  return true;
};

class ServerCallbacks: public BLEServerCallbacks {
 public:
    bool* _pConnected;

    ServerCallbacks(bool* connected) {
      _pConnected = connected;
    }
    void onConnect(BLEServer* pServer) {
      *_pConnected = true;
      Serial.println("ServerCallbacks onConnect");
      M5.Lcd.println("ServerCallbacks onConnect");
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 0);
    }
    void onDisconnect(BLEServer* pServer) {
      *_pConnected = false;
      Serial.println("ServerCallbacks onDisconnect");
      M5.Lcd.println("ServerCallbacks onDisconnect");
    }
};

BLEServer* createServer(char* name, bool* pConnected) {
  BLEDevice::init(name);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks(pConnected));  
};

std::string toMACAddrString(char* buf, uint8_t* mac) {
  size_t len = sprintf(buf, " %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); 
  return std::string(buf, len);
};

std::string toUInt8String(char* buf, uint8_t v) {
  size_t len = sprintf(buf, " %04d", v); 
  return std::string(buf, len);  
};

class InformationCharacteristic : public BLECharacteristic {
public:
  InformationCharacteristic(BLEUUID uuid, std::string value) : BLECharacteristic(uuid) {
    this->setReadProperty(true);
    this->setValue(value);
  }
};

BLEService* createInformationService(BLEServer* pServer) {
  BLEService* pService = pServer->createService(BLEUUID((uint16_t)0x180a));
  uint8_t mac[6];
  char buf[256];
  esp_efuse_mac_get_default(mac);
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a23), "System ID"));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a25), toMACAddrString(buf, mac)));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a26), esp_get_idf_version()));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a27), toUInt8String(buf, ESP.getChipRevision())));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a28), "Software Rev"));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a29), "M5Stack")); 
  return pService;
};

class BatteryLevelCharacteristic : public BLECharacteristic {
public:
  BatteryLevelCharacteristic() : BLECharacteristic(BLEUUID((uint16_t)0x2a19)) {  
    this->setReadProperty(true);
    this->setNotifyProperty(true);
     
    BLE2904 *pDescriptor = new BLE2904();
    pDescriptor->setFormat(BLE2904::FORMAT_UINT8);
    pDescriptor->setNamespace(1);
    pDescriptor->setUnit(0x27ad);               
    this->addDescriptor(pDescriptor);
  }
};

BLEService* createBatteryService(BLEServer* pServer, BLECharacteristic* pCharacteristic) {
  BLEService* pService = pServer->createService(BLEUUID((uint16_t)0x180F));
  pService->addCharacteristic(pCharacteristic);
  return pService;
};

uint8_t keysState = 0x00;

bool SimpleKeysGetData() {
  uint8_t value = 0x00;
  value |= M5.BtnA.wasReleased() ? 0x01 : 0x00;
  value |= M5.BtnB.wasReleased() ? 0x02 : 0x00;
  value |= M5.BtnC.wasReleased() ? 0x04 : 0x00;
  keysState = value;
  return true;
}

class SimpleKeysCharacteristic : public BLECharacteristic {
public:
  SimpleKeysCharacteristic() : BLECharacteristic(BLEUUID((uint16_t)0xffe1)) {
    this->setReadProperty(true);
    this->setNotifyProperty(true);
  }
};

BLEService* createSimpleKeysService(BLEServer* pServer, BLECharacteristic* pCharacteristic) {
  BLEService* pService = pServer->createService(BLEUUID((uint16_t)0xffe0));
  pService->addCharacteristic(pCharacteristic);
  return pService;
};

class SensorCharacteristic : public BLECharacteristic {
private:
  int lastUpdate;
  void updateTime() {
    this->lastUpdate = millis();
  }
public:
  SensorCharacteristic(const char* uuid) : BLECharacteristic(uuid) {
    this->setReadProperty(true);
    this->setNotifyProperty(true);
  }
  SensorCharacteristic(const char* uuid, BLECharacteristicCallbacks* pCallbacks) : SensorCharacteristic(uuid) {
    this->setCallbacks(pCallbacks);
  }
  bool elapsed(int msecInterval) {
    int current = millis();
    bool ret = (current - this->lastUpdate) > msecInterval;
    Serial.print(current);
    Serial.print(" - ");Serial.print(this->lastUpdate);
    Serial.print(" > ");Serial.print(msecInterval);
    Serial.print(" = ");Serial.println(ret);
    return ret;
  }
  void setValue(uint8_t* data, size_t size) {
    this->BLECharacteristic::setValue(data, size);
    this->updateTime();
  }
  void setValue(uint8_t value) {
    this->BLECharacteristic::setValue(&value, 1);
    this->updateTime();
  }
  void setValue(uint16_t value) {
    this->BLECharacteristic::setValue(value);
    this->updateTime();
  }
  void setValue(uint32_t value) {
    this->BLECharacteristic::setValue(value);
    this->updateTime();
  }
};

class UInt8ConfigCharacteristic : public BLECharacteristic {
  int lastUpdate;
public:
  UInt8ConfigCharacteristic(const char* uuid, uint8_t value) : BLECharacteristic(uuid) {
    this->setReadProperty(true);
    this->setWriteProperty(true);
    this->setValue(&value, 1);   
  }
};

class PeriodCharacteristic : public UInt8ConfigCharacteristic {
public:
  PeriodCharacteristic(const char* uuid, uint8_t value) : UInt8ConfigCharacteristic(uuid, value) {
  }
  uint8_t getPeriod() { // 1 = 10msec
    return this->getData()[0];
  }
};

class UInt16ConfigCharacteristic : public BLECharacteristic {
public:
  UInt16ConfigCharacteristic(const char* uuid, uint16_t initialValue) : BLECharacteristic(uuid) {
    this->setReadProperty(true);
    this->setWriteProperty(true);
    this->setValue(initialValue);
  }
};

const char* temperature_sensor = "f000aa00-0451-4000-b000-000000000000";
const char* temperature_data   = "f000aa01-0451-4000-b000-000000000000";
const char* temperature_config = "f000aa02-0451-4000-b000-000000000000";
const char* temperature_period = "f000aa03-0451-4000-b000-000000000000";

BLEService* createTemperatureService(BLEServer* pServer, BLECharacteristic* pData, BLECharacteristic* pPeriod) {
  BLEService* pService = pServer->createService(temperature_sensor);
  pService->addCharacteristic(pData);
  pService->addCharacteristic(new UInt8ConfigCharacteristic(temperature_config, 0x01));
  pService->addCharacteristic(pPeriod);
  return pService;  
};

BLEService* createTemperatureService(BLEServer* pServer, BLECharacteristic* pData) {
    BLECharacteristic* pPeriod = new PeriodCharacteristic(temperature_period, 0x64);
    return createTemperatureService(pServer, pData, pPeriod);
};

const char* movement_sensor = "f000aa80-0451-4000-b000-000000000000";
const char* movement_data   = "f000aa81-0451-4000-b000-000000000000";
const char* movement_config = "f000aa82-0451-4000-b000-000000000000";
const char* movement_period = "f000aa83-0451-4000-b000-000000000000";

BLEService* createMovementService(BLEServer* pServer, BLECharacteristic* pData, BLECharacteristic* pPeriod) {
  BLEService* pService = pServer->createService(movement_sensor);
  pService->addCharacteristic(pData);
  pService->addCharacteristic(new UInt16ConfigCharacteristic(movement_config, 0x00ff));
  pService->addCharacteristic(pPeriod);
  return pService;
};

BLEService* createMovementService(BLEServer* pServer, BLECharacteristic* pData) {
  BLECharacteristic* pPeriod = new PeriodCharacteristic(movement_period, 0x64);
  return createMovementService(pServer, pData, pPeriod);
};

const char* humidity_sensor = "f000aa20-0451-4000-b000-000000000000";
const char* humidity_data   = "f000aa21-0451-4000-b000-000000000000";
const char* humidity_config = "f000aa22-0451-4000-b000-000000000000";
const char* humidity_period = "f000aa23-0451-4000-b000-000000000000";

BLEService* createHumidityService(BLEServer* pServer, BLECharacteristic* pData, BLECharacteristic* pPeriod) {
  BLEService* pService = pServer->createService(humidity_sensor);
  pService->addCharacteristic(pData);
  pService->addCharacteristic(new UInt8ConfigCharacteristic(humidity_config, 0x01));
  pService->addCharacteristic(pPeriod);
  return pService; 
};

BLEService* createHumidityService(BLEServer* pServer, BLECharacteristic* pData) {
  BLECharacteristic* pPeriod = new PeriodCharacteristic(humidity_period, 0x1e);
  return createHumidityService(pServer, pData, pPeriod);
};

const char* barometer_sensor = "f000aa40-0451-4000-b000-000000000000";
const char* barometer_data   = "f000aa41-0451-4000-b000-000000000000";
const char* barometer_config = "f000aa42-0451-4000-b000-000000000000";
const char* barometer_period = "f000aa44-0451-4000-b000-000000000000";

BLEService* createBarometerService(BLEServer* pServer, BLECharacteristic* pData, BLECharacteristic* pPeriod) {
  BLEService* pService = pServer->createService(barometer_sensor);
  pService->addCharacteristic(pData);
  pService->addCharacteristic(new UInt8ConfigCharacteristic(barometer_config, 0x00));
  pService->addCharacteristic(pPeriod);
  return pService;
};

BLEService* createBarometerService(BLEServer* pServer, BLECharacteristic* pData) {
  BLECharacteristic* pPeriod = new PeriodCharacteristic(humidity_period, 0x1e);
  return createBarometerService(pServer, pData, pPeriod);  
};

const char* io_service = "f000aa64-0451-4000-b000-000000000000";
const char* io_data    = "f000aa65-0451-4000-b000-000000000000";
const char* io_config  = "f000aa66-0451-4000-b000-000000000000";

#define M5STACK_FIRE_NEO_NUM_LEDS 10
#define M5STACK_FIRE_NEO_DATA_PIN 15

Adafruit_NeoPixel Pixels = Adafruit_NeoPixel(M5STACK_FIRE_NEO_NUM_LEDS, M5STACK_FIRE_NEO_DATA_PIN, NEO_GRB + NEO_KHZ800);

class IOCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pCharacteristic) {
  }
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t value = pCharacteristic->getValue()[0];
     Serial.print("IO Value: ");
     Serial.println(value);
     {
       bool red = (value & 0x01) > 0;
       uint32_t color = red ? 0x00ff0000 : 0x00000000;
       for (int i=0; i<5;i++) { Pixels.setPixelColor(i, color); }
     }
     {
       bool green = (value & 0x02) > 0;
       uint32_t color = green ? 0x0000ff00 : 0x00000000;
       for (int i=0; i<10;i++) { Pixels.setPixelColor(i, color); }
     }
     bool buzzer = (value & 0x04) > 0;
     if (buzzer) {
       M5.Speaker.tone(440);
     } else {
       M5.Speaker.end();
     }
  }
};

class IOCharacteristic : public BLECharacteristic {
public:
  IOCharacteristic(BLECharacteristicCallbacks* pCallbacks) : BLECharacteristic(io_data) {
    this->setCallbacks(pCallbacks);
    this->setReadProperty(true);
    this->setWriteProperty(true);
    uint8_t mode = 0x01;
    this->setValue(&mode, 1);
  }
};

BLEService* createIOService(BLEServer* pServer) {
   BLEService* pService = pServer->createService(BLEUUID(io_service));
   pService->addCharacteristic(new IOCharacteristic(new IOCallbacks()));
   pService->addCharacteristic(new UInt8ConfigCharacteristic(io_config, 0x00));
   return pService;
 };
 
bool isDHT12 = false;
bool isBME280 = false;
bool isMPU9250 = true;

bool deviceConnected = false;
BLECharacteristic* pBatteryLevelCharacteristic = NULL;
BLECharacteristic* pSimpleKeysCharacteristic   = NULL;
SensorCharacteristic* pTemperatureData         = NULL;
PeriodCharacteristic* pTemperaturePeriod       = NULL;
SensorCharacteristic* pMovementData            = NULL;
PeriodCharacteristic* pMovementPeriod          = NULL;
SensorCharacteristic* pHumidityData            = NULL;
PeriodCharacteristic* pHumidityPeriod          = NULL;
SensorCharacteristic* pBarometerData           = NULL;
PeriodCharacteristic* pBarometerPeriod         = NULL;

void setup() {
  M5.begin();
  M5.Power.begin();
  Wire.begin();
  
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);
  
  M5.Lcd.println("enter setup");
  Serial.println("enter setup");

  if (isBME280) {
    while (!bme.begin(0x76)){  
      Serial.println("Could not find a valid BMP280 sensor, check wiring!");
      M5.Lcd.println("Could not find a valid BMP280 sensor, check wiring!");
    }
  }

  if (isMPU9250) {
    IMU.calibrateMPU9250(IMU.gyroBias, IMU.accelBias);
    IMU.initMPU9250();
    IMU.initAK8963(IMU.magCalibration);
    filter.begin(1);    
  }
  
  BLEServer *pServer = createServer("M5Stack Fire", &deviceConnected);

  BLEService* pService = createInformationService(pServer);
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a24), "M5Stack Fire"));
  pService->start();
  M5.Lcd.println("Information Service start");
  Serial.println("Information Service start");

  pBatteryLevelCharacteristic = new BatteryLevelCharacteristic();
  createBatteryService(pServer, pBatteryLevelCharacteristic)->start();
  M5.Lcd.println("Battery Service start");
  Serial.println("Battery Service start");

  pSimpleKeysCharacteristic = new SimpleKeysCharacteristic();
  createSimpleKeysService(pServer, pSimpleKeysCharacteristic)->start();
  M5.Lcd.println("SimpleKeys Service start");
  Serial.println("SimpleKeys Service start");
  
  if (isDHT12) {
    pTemperatureData = new SensorCharacteristic(temperature_data);
    pTemperaturePeriod = new PeriodCharacteristic(temperature_period, 0x64);
    createTemperatureService(pServer, pTemperatureData, pTemperaturePeriod)->start();
    M5.Lcd.println("Temperature Service start");
    Serial.println("Temperature Service start");
  }

  if (isMPU9250) {
    pMovementData = new SensorCharacteristic(movement_data);
    pMovementPeriod = new PeriodCharacteristic(movement_period, 0x64);
    createMovementService(pServer, pMovementData, pMovementPeriod)->start();
    M5.Lcd.println("Movement Service start");
    Serial.println("Movement Service start");   
  }
  
  if (isDHT12) {
    pHumidityData = new SensorCharacteristic(humidity_sensor);
    pHumidityPeriod = new PeriodCharacteristic(humidity_period, 0x64);
    createHumidityService(pServer, pHumidityData, pHumidityPeriod)->start();
    M5.Lcd.println("Humidity Service start");
    Serial.println("Humidity Service start");
  }

  if (isBME280) {
    pBarometerData = new SensorCharacteristic(barometer_data);
    pBarometerPeriod = new PeriodCharacteristic(barometer_period, 0x64);
    createBarometerService(pServer, pBarometerData, pBarometerPeriod)->start();
    M5.Lcd.println("Barometer Service start");
    Serial.println("Barometer Service start");
  }

  if (true) {
    Pixels.begin();
    Pixels.show();
    M5.Speaker.begin();
    createIOService(pServer)->start();
    M5.Lcd.println("IO Service start");
    Serial.println("IO Service start"); 
  }
  
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
  M5.Lcd.println("Advertising start");
  Serial.println("Advertising start");
  
  M5.Lcd.println("exit setup");
  Serial.println("exit setup");
}

void loop() {
  M5.Lcd.setCursor(0, 0);

  uint8_t battery = M5.Power.getBatteryLevel();
  pBatteryLevelCharacteristic->setValue(&battery, 1);
  
  uint8_t prevState = keysState;
  SimpleKeysGetData();
  if (keysState != prevState) {
    pSimpleKeysCharacteristic->setValue(&keysState, 1);
    pSimpleKeysCharacteristic->notify();
    if (keysState != 0x00) {
      String msg = "Button was released";
      M5.Lcd.println(msg);
      Serial.println(msg);
    } 
  }
  
  if (isDHT12) {
    DHT12ReadData();
    Serial.printf("Temperature[degC]: %2.2f\r\n", dht12Temperature);
    M5.Lcd.printf("Temperature[degC]: %2.2f\r\n", dht12Temperature);
    Serial.printf("Humidity[%%]:      %0.2f\r\n", dht12Humidity);
    M5.Lcd.printf("Humidity[%%]:      %0.2f\r\n", dht12Humidity);
          
    if ((pTemperatureData != NULL) && (pTemperatureData->elapsed(pTemperaturePeriod->getPeriod()*10) > 0)) {
      uint16_t value = (int)(dht12Temperature / 0.03125) << 2;
      pTemperatureData->setValue(value);
      pTemperatureData->notify(value);
    }
    if ((pHumidityData != NULL) && (pHumidityData->elapsed(pHumidityPeriod->getPeriod()*10) > 0)) {
      int16_t value[2];
      value[0] = (dht12Temperature + 40)*65536 /165;
      value[1] = dht12Humidity*65536/100;
      pHumidityData->setValue((uint8_t*)value, 4);
      pHumidityData->notify();
    }
  }

  if (isBME280) {
    BMP280ReadData();
    Serial.printf("Pressure[Pa]: %0.2f\r\n", bmePressure);
    M5.Lcd.printf("Pressure[Pa]: %0.2f\r\n", bmePressure);
     
    if ((pBarometerData != NULL) && (pBarometerData->elapsed(pBarometerPeriod->getPeriod()*10) > 0)) {
      uint32_t tInt = dht12Temperature*100;
      uint8_t* t = (uint8_t*)&tInt;
      uint32_t pInt = bmePressure;
      uint8_t* p = (uint8_t*)&pInt;
      uint8_t value[6];
      value[0] = *(t + 0);
      value[1] = *(t + 1);
      value[2] = *(t + 2);
      value[3] = *(p + 0);
      value[4] = *(p + 1);
      value[5] = *(p + 2);
      pBarometerData->setValue(value, 6);   
      pBarometerData->notify();   
    }
  }
  if (isMPU9250) {
    MPU9250ReadData();
    Serial.printf("A: %5.1f  %5.1f  %5.1f\r\n", IMU.ax,IMU.ay,IMU.az); // [g]
    M5.Lcd.printf("A: %5.1f  %5.1f  %5.1f\r\n", IMU.ax,IMU.ay,IMU.az);
    Serial.printf("G: %5.1f  %5.1f  %5.1f\r\n", IMU.gx,IMU.gy,IMU.gz); // [o/s]
    M5.Lcd.printf("G: %5.1f  %5.1f  %5.1f\r\n", IMU.gx,IMU.gy,IMU.gz);
    Serial.printf("M: %5.1f  %5.1f  %5.1f\r\n", IMU.mx/1000,IMU.my/1000,IMU.mz/1000);
    M5.Lcd.printf("M: %5.1f  %5.1f  %5.1f\r\n", IMU.mx/1000,IMU.my/1000,IMU.mz/1000);
    Serial.printf("R: %5.1f  %5.1f  %5.1f\r\n", IMU.roll,IMU.pitch,IMU.yaw);
    M5.Lcd.printf("R: %5.1f  %5.1f  %5.1f\r\n", IMU.roll,IMU.pitch,IMU.yaw); // [G]
    Serial.printf("Temp[degC]: %2.2f\r\n", IMU.temperature);
    M5.Lcd.printf("Temp[degC]: %2.2f\r\n", IMU.temperature);
          
    if ((pMovementData != NULL) && (pMovementData->elapsed(pMovementPeriod->getPeriod()*10) > 0)) {
      int16_t value[9];
      value[0] = (int16_t)(IMU.gx*65536/500);
      value[1] = (int16_t)(IMU.gy*65536/500);
      value[2] = (int16_t)(IMU.gz*65536/500);
      value[3] = (int16_t)(IMU.ax*32768/8);
      value[4] = (int16_t)(IMU.ay*32768/8);
      value[5] = (int16_t)(IMU.az*32768/8);
      value[6] = (int16_t)(IMU.mx/10*8190/32768);
      value[7] = (int16_t)(IMU.my/10*8190/32768);
      value[8] = (int16_t)(IMU.mz/10*8190/32768);      
      pMovementData->setValue((unsigned char*)value, sizeof(value));
      pMovementData->notify();
    }
  }
  
  if (M5.BtnC.wasPressed()) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
  }
  
  delay(100);
  M5.update();
}
