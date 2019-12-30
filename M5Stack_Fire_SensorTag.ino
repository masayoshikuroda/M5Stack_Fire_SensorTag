#include <M5Stack.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2904.h>
#include <Adafruit_NeoPixel.h>
#include "utility/MPU9250.h"
#include <MadgwickAHRS.h>





class ServerCallbacks: public BLEServerCallbacks {
 public:
    bool* _pConnected;

    ServerCallbacks(bool* connected) {
      _pConnected = connected;
    }
    void onConnect(BLEServer* pServer) {
      *_pConnected = true;
      M5.Lcd.println("ServerCallbacks onConnect");
      Serial.println("ServerCallbacks onConnect");
    }
    void onDisconnect(BLEServer* pServer) {
      *_pConnected = false;
      M5.Lcd.println("ServerCallbacks onDisconnect");
      Serial.println("ServerCallbacks onDisconnect");
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
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a24), "M5Stack Fire"));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a25), toMACAddrString(buf, mac)));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a26), esp_get_idf_version()));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a27), toUInt8String(buf, ESP.getChipRevision())));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a28), "Software Rev"));
  pService->addCharacteristic(new InformationCharacteristic(BLEUUID((uint16_t)0x2a29), "M5Stack")); 
  return pService;
};




class BatteryLevelCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pCharacteristic) {
    uint8_t level = M5.Power.getBatteryLevel();
    pCharacteristic->setValue(&level, 1);
  }
  void onNotify(BLECharacteristic *pCharacteristic) {
    onRead(pCharacteristic);
  }
};

class BatteryLevelCharacteristic : public BLECharacteristic {
public:
  BatteryLevelCharacteristic(BLECharacteristicCallbacks* pCallbacks) : BLECharacteristic(BLEUUID((uint16_t)0x2a19)) {
    this->setCallbacks(pCallbacks);    
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





class SimpleKeysCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pCharacteristic) {
    uint8_t value = 0x00;
    value |= M5.BtnA.wasReleased() ? 0x01 : 0x00;
    value |= M5.BtnB.wasReleased() ? 0x02 : 0x00;
    value |= M5.BtnC.wasReleased() ? 0x04 : 0x00;
    pCharacteristic->setValue(&value, 1);
  }
  void onNotify(BLECharacteristic *pCharacteristic) {
    onRead(pCharacteristic);
    uint8_t value = *pCharacteristic->getData();
    if (value != 0x00) {
      M5.Lcd.println("Button was released");
      Serial.println("Button was released");
    }   
  }
};

class SimpleKeysCharacteristic : public BLECharacteristic {
public:
  SimpleKeysCharacteristic(BLECharacteristicCallbacks* pCallbacks) : BLECharacteristic(BLEUUID((uint16_t)0xffe1)) {
    this->setReadProperty(true);
    this->setNotifyProperty(true);
    this->setCallbacks(pCallbacks);
  }
};

BLEService* createSimpleKeysService(BLEServer* pServer, BLECharacteristic* pCharacteristic) {
  BLEService* pService = pServer->createService(BLEUUID((uint16_t)0xffe0));
  pService->addCharacteristic(pCharacteristic);
  return pService;
};





MPU9250 IMU;
Madgwick filter;

#define movement_sensor "f000aa80-0451-4000-b000-000000000000"
#define movement_data   "f000aa81-0451-4000-b000-000000000000"
#define movement_config "f000aa82-0451-4000-b000-000000000000"
#define movement_period "f000aa83-0451-4000-b000-000000000000"

class MovementCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pCharacteristic) {
    if (IMU.readByte(MPU9250_ADDRESS, INT_STATUS) & 0x01) {
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
      pCharacteristic->setValue((unsigned char*)value, sizeof(value));

      Serial.print(IMU.ax); Serial.print(", ");
      Serial.print(IMU.ay); Serial.print(", ");
      Serial.print(IMU.az); Serial.print(", ");
      Serial.print(IMU.gx); Serial.print(", ");
      Serial.print(IMU.gy); Serial.print(", ");
      Serial.print(IMU.gz); Serial.print(", ");
      Serial.print(IMU.mx); Serial.print(", ");
      Serial.print(IMU.my); Serial.print(", ");
      Serial.print(IMU.mz); 
      Serial.println("");
    }
  }
  void onNotify(BLECharacteristic *pCharacteristic) { 
      onRead(pCharacteristic);
  }
};

class MovementCharacteristic : public BLECharacteristic {
public:
  MovementCharacteristic(BLECharacteristicCallbacks* pCallbacks) : BLECharacteristic(movement_data) {
    this->setCallbacks(pCallbacks);
    this->setReadProperty(true);
    this->setNotifyProperty(true);
  }
};

class MovementConfigCharacteristic : public BLECharacteristic {
public:
  MovementConfigCharacteristic() : BLECharacteristic(movement_config) {
    this->setReadProperty(true);
    this->setWriteProperty(true);
    uint16_t value = 0x00FF;
    this->setValue(value);
  }  
};

class MovementPeriodCharacteristic : public BLECharacteristic {
public:
  MovementPeriodCharacteristic() : BLECharacteristic(movement_period) {
    this->setReadProperty(true);
    this->setWriteProperty(true);
    uint8_t value = 0x1E;
    this->setValue(&value, 1);
  }  
};

BLEService* createMovementService(BLEServer* pServer, BLECharacteristic* pCharacteristic) {
  BLEService* pService = pServer->createService(movement_sensor);
  pService->addCharacteristic(pCharacteristic);
  pService->addCharacteristic(new MovementConfigCharacteristic());
  pService->addCharacteristic(new MovementPeriodCharacteristic());
  return pService;
};






#define M5STACK_FIRE_NEO_NUM_LEDS 10
#define M5STACK_FIRE_NEO_DATA_PIN 15

Adafruit_NeoPixel Pixels = Adafruit_NeoPixel(
  M5STACK_FIRE_NEO_NUM_LEDS,
  M5STACK_FIRE_NEO_DATA_PIN,
  NEO_GRB + NEO_KHZ800
);

#define io_service "f000aa64-0451-4000-b000-000000000000"
#define io_data    "f000aa65-0451-4000-b000-000000000000"
#define io_config  "f000aa66-0451-4000-b000-000000000000"

class IOCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pCharacteristic) {
  }
  void onWrite(BLECharacteristic *pCharacteristic) {
    uint8_t value = pCharacteristic->getValue()[0];
    Serial.print(value);
    Serial.println("");
    bool red = (value & 0x01) > 0;
    if (red) {
      Pixels.setPixelColor(0, 0x00ff0000);
      Pixels.setPixelColor(1, 0x00ff0000);
      Pixels.setPixelColor(2, 0x00ff0000);
      Pixels.setPixelColor(3, 0x00ff0000);
      Pixels.setPixelColor(4, 0x00ff0000);  
      M5.Lcd.println("Red LED on");
      Serial.println("Red LED on");
    } else {
      Pixels.setPixelColor(0, 0x00000000); 
      Pixels.setPixelColor(1, 0x00000000); 
      Pixels.setPixelColor(2, 0x00000000); 
      Pixels.setPixelColor(3, 0x00000000); 
      Pixels.setPixelColor(4, 0x00000000);  
      M5.Lcd.println("Red LED off");
      Serial.println("Red LED off");         
    }
    bool green = (value & 0x02) > 0;
    if (green) {
      Pixels.setPixelColor(5, 0x0000ff00);
      Pixels.setPixelColor(6, 0x0000ff00);
      Pixels.setPixelColor(7, 0x0000ff00);
      Pixels.setPixelColor(8, 0x0000ff00);
      Pixels.setPixelColor(9, 0x0000ff00);
      M5.Lcd.println("Green LED on");
      Serial.println("Green LED on");      
    } else {
      Pixels.setPixelColor(5, 0x00000000);
      Pixels.setPixelColor(6, 0x00000000);
      Pixels.setPixelColor(7, 0x00000000);
      Pixels.setPixelColor(8, 0x00000000);
      Pixels.setPixelColor(9, 0x00000000); 
      M5.Lcd.println("Green LED off");
      Serial.println("Green LED off");         
    }
    bool buzzer = (value & 0x04) > 0;
    if (buzzer) {
      M5.Speaker.tone(440);
      M5.Lcd.println("Buzzer on");
      Serial.println("Buzzer on");      
    } else {
      M5.Speaker.mute();
      M5.Lcd.println("Buzzer off");
      Serial.println("Buzzer off");         
    }        
    Pixels.show();   
  }
};

class IOCharacteristic : public BLECharacteristic {
public:
  IOCharacteristic(BLECharacteristicCallbacks* pCallbacks) : BLECharacteristic(BLEUUID(io_data)) {
    this->setCallbacks(pCallbacks);
    this->setReadProperty(true);
    this->setWriteProperty(true);
  }
};

class IOConfigCharacteristic : public BLECharacteristic {
public:
  IOConfigCharacteristic() : BLECharacteristic(BLEUUID(io_config)) {
    this->setReadProperty(true);
    this->setWriteProperty(true);
    uint8_t mode = 0x01;
    this->setValue(&mode, 1);
  }
};

BLEService* createIOService(BLEServer* pServer) {
  BLEService* pService = pServer->createService(BLEUUID(io_service));
  pService->addCharacteristic(new IOCharacteristic(new IOCallbacks()));
  pService->addCharacteristic(new IOConfigCharacteristic());
  return pService;
};





#define neopixel_service    "209b0000-13f8-43d4-a0ed-02b2c44e6fd4"
#define neopixel_brightness "209b0001-13f8-43d4-a0ed-02b2c44e6fd4"
#define neopixel_color      "209b%04x-13f8-43d4-a0ed-02b2c44e6fd4"

class NeoPixelBrightnessCallbacks: public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic *pCharacteristic) {
    uint32_t brightness = Pixels.getBrightness();
    pCharacteristic->setValue(brightness);
  }
  void onWrite(BLECharacteristic *pCharacteristic) {
    const char *value = pCharacteristic->getValue().c_str();
    uint32_t* pbrightness = (uint32_t*)value;
    Pixels.setBrightness(*pbrightness);     
    Pixels.show();
  }
  void onNotify(BLECharacteristic *pCharacteristic) {
    onRead(pCharacteristic);
  }
};

class NeoPixelBrightnessCharacteristic : public BLECharacteristic {
public:
  NeoPixelBrightnessCharacteristic(BLECharacteristicCallbacks* pCallbacks) : BLECharacteristic(BLEUUID(neopixel_brightness)) {
    this->setCallbacks(pCallbacks);
    this->setReadProperty(true);
    this->setWriteProperty(true);
  }
};

class NeoPixelColorCallbacks: public BLECharacteristicCallbacks {
private:
  int _led;
public:
  NeoPixelColorCallbacks(int led) {
    _led = led;
  }
  void onRead(BLECharacteristic *pCharacteristic) {
    uint32_t color = Pixels.getPixelColor(_led);
    pCharacteristic->setValue(color);
  }
  void onWrite(BLECharacteristic *pCharacteristic) {
    const char *value = pCharacteristic->getValue().c_str();
    uint32_t* pcolor = (uint32_t*)value;
    Pixels.setPixelColor(_led, *pcolor);     
    Pixels.show();
  }
  void onNotify(BLECharacteristic* pCharacteristic) {
    onRead(pCharacteristic);
  }
};

class NeoPixelColorCharacteristic : public BLECharacteristic {
public:
  NeoPixelColorCharacteristic(char* uuid, BLECharacteristicCallbacks* pCallbacks) : BLECharacteristic(BLEUUID(uuid)) {
    this->setReadProperty(true);
    this->setWriteProperty(true);
    this->setCallbacks(pCallbacks);
  }
};

BLEService* createNeoPixelService(BLEServer* pServer) {
  BLEService* pService = pServer->createService(BLEUUID(neopixel_service));
  pService->addCharacteristic(new NeoPixelBrightnessCharacteristic(new NeoPixelBrightnessCallbacks()));
  for (int led=0; led<M5STACK_FIRE_NEO_NUM_LEDS; led++) {
    char uuid[256];
    sprintf(uuid, neopixel_color, led + 2);
    pService->addCharacteristic(new NeoPixelColorCharacteristic(uuid, new NeoPixelColorCallbacks(led)));
  }
  return pService;
};




bool deviceConnected = false;
BatteryLevelCharacteristic* pBatteryLevelCharacteristic;
SimpleKeysCharacteristic* pSimpleKeysCharacteristic;
MovementCharacteristic* pMovementCharacteristic;

void setup() {
  M5.begin();

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);
  
  M5.Lcd.println("enter setup");
  Serial.println("enter setup");

  BLEServer *pServer = createServer("M5Stack Fire", &deviceConnected); 

  createInformationService(pServer)->start();
  M5.Lcd.println("Information Service start");
  Serial.println("Information Service start");

  M5.Power.begin();
  pBatteryLevelCharacteristic = new BatteryLevelCharacteristic(new BatteryLevelCallbacks());
  createBatteryService(pServer, pBatteryLevelCharacteristic)->start();
  M5.Lcd.println("Battery Service start");
  Serial.println("Battery Service start");

  pSimpleKeysCharacteristic = new SimpleKeysCharacteristic(new SimpleKeysCallbacks());
  createSimpleKeysService(pServer, pSimpleKeysCharacteristic)->start();
  M5.Lcd.println("SimpleKeys Service start");
  Serial.println("SimpleKeys Service start");

  IMU.calibrateMPU9250(IMU.gyroBias, IMU.accelBias);
  IMU.initMPU9250();
  IMU.initAK8963(IMU.magCalibration);
  filter.begin(1);
  pMovementCharacteristic = new MovementCharacteristic(new MovementCallbacks());
  createMovementService(pServer, pMovementCharacteristic)->start();
  M5.Lcd.println("Movement Service start");
  Serial.println("Movement Service start");

  Pixels.begin();
  Pixels.show();
  createIOService(pServer)->start();
  M5.Lcd.println("IO Service start");
  Serial.println("IO Service start");  

  createNeoPixelService(pServer)->start();
  M5.Lcd.println("NeoPixel Service start");
  Serial.println("NeoPixel Service start");

  
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
  M5.Lcd.println("Advertising start");
  Serial.println("Advertising start");
  
  M5.Lcd.println("exit setup");
  Serial.println("exit setup");
}

void loop() {
  //Serial.println("enter loop");


    //Serial.printf("notify %d\n", level);
    
    pBatteryLevelCharacteristic->notify();
    pSimpleKeysCharacteristic->notify();
    pMovementCharacteristic->notify();
  }
  delay(300);
  M5.update();
}