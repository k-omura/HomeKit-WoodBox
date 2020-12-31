
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Wire.h>

//Power LED
#define LEDC_CHANNEL_0 0 //channel max 15
#define LEDC_TIMER_BIT 8 //8bit: 最大周波数312500Hz
#define LEDC_BASE_FREQ 312500.0
#define GPIO_PIN 33 //GPIO
#define DIM_MIN_VAL 0
#define DIM_MAX_VAL 0xFF

//Status LEDs
#define STATUS_PIN1 19 //1
#define STATUS_PIN2 32 //2
#define STATUS_PIN3 25 //3
#define STATUS_PIN4 26 //4

//touch
#define THRESHOLD_RATIO 0.9
#define TOUCH_PIN1 27
#define TOUCH_PIN2 14
#define TOUCH_PIN3 12
#define TOUCH_PIN4 13

//AXDL345
#define INT1_PIN 16
#define INT2_PIN 17
uint8_t DEVICE_ADDRESS = 0x53;
//uint8_t DEVICE_ADDRESS = 0x1D;

const char* ssid     = "";
const char* password = "";

const int identity_led = 5;
const int led_gpio = 33;

extern "C" {
#include "homeintegration.h"
}
homekit_service_t* hapservice = {0};
String pair_file_name = "/pair.dat";

uint8_t INT1Rised = 0;
void IRAM_ATTR interruptInt1() {
  INT1Rised = 1;
}
uint8_t INT2Rised = 0;
void IRAM_ATTR interruptInt2() {
  INT2Rised = 1;
}

uint8_t touchedPIN = 0;
void interruptTouch1() {
  touchedPIN = 1;
  //Serial.print("----------");
}
void interruptTouch2() {
  touchedPIN = 2;
}
void interruptTouch3() {
  touchedPIN = 3;
}
void interruptTouch4() {
  touchedPIN = 4;
}

uint8_t controlStatusLEDs(uint8_t state) {
  ledcWrite(1, 0);
  ledcWrite(2, 0);
  ledcWrite(3, 0);
  ledcWrite(4, 0);

  switch (state) {
    case 4:
      ledcWrite(4, 0xff);
    case 3:
      ledcWrite(3, 0xff);
    case 2:
      ledcWrite(2, 0xff);
    case 1:
      ledcWrite(1, 0xff);
      break;
    default:
      break;
  }
}

uint8_t targetDuty = 0;
uint8_t dutyBeforeOff = 20;
uint8_t controlDuty() {
  static uint8_t currentDuty = 0;
  static uint8_t targeDutyRaw = 0;

  targeDutyRaw = map(targetDuty, 0, 100, DIM_MIN_VAL, DIM_MAX_VAL);

  if (currentDuty == targeDutyRaw) {
    return currentDuty;
  } else if (currentDuty < targeDutyRaw) {
    currentDuty++;
  } else if (currentDuty > targeDutyRaw) {
    currentDuty--;
  } else {
    currentDuty = 0;
    targeDutyRaw = 0;
  }

  ledcWrite(LEDC_CHANNEL_0, currentDuty);
  delay(5);

  return currentDuty;
}

uint8_t touchMax(uint8_t pinNum) {
  uint8_t touchReadVal, touchReadValMin = 100;

  for (uint8_t i = 0; i < 10; i++) {
    touchReadVal = touchRead(pinNum);
    if (touchReadValMin > touchReadVal) {
      touchReadValMin = touchReadVal;
    }
  }

  return touchReadValMin;
}

void setup() {
  Serial.begin(115200);
  delay(10);

  Wire.begin();

  //--AXDL345 configuration
  //THRESH_TAP
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x1d);
  Wire.write(30);
  Wire.endTransmission();

  //DUR
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x21);
  Wire.write(10);
  Wire.endTransmission();

  //Latent
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x22);
  Wire.write(80);
  Wire.endTransmission();

  //Window
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x23);
  Wire.write(0xff);
  Wire.endTransmission();

  //TAP_AXES
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x2a);
  Wire.write(0b00000111);
  Wire.endTransmission();

  //INT_ENABLE
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x2e);
  Wire.write(0b01100000);
  Wire.endTransmission();

  //INT_MAP
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x2f);
  Wire.write(0b00000000);
  Wire.endTransmission();

  //DATA_FORMAT
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x31);
  Wire.write(0x0b);
  Wire.endTransmission();

  //POWER_CTL
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0x2d);
  Wire.write(0b00001000);
  Wire.endTransmission();

  //interrupt
  attachInterrupt(INT1_PIN, interruptInt1, RISING); //INT1
  attachInterrupt(INT2_PIN, interruptInt2, RISING); //INT2
  //--AXDL345 configuration end

  //---Power LED config
  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(GPIO_PIN, LEDC_CHANNEL_0);
  ledcWrite(LEDC_CHANNEL_0, 0);
  //---Power LED end

  //---Status LEDs config
  ledcSetup(1, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(STATUS_PIN1, 1);
  ledcWrite(1, 0xff);

  ledcSetup(2, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(STATUS_PIN2, 2);
  ledcWrite(2, 0xff);

  ledcSetup(3, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(STATUS_PIN3, 3);
  ledcWrite(3, 0xff);

  ledcSetup(4, LEDC_BASE_FREQ, LEDC_TIMER_BIT);
  ledcAttachPin(STATUS_PIN4, 4);
  ledcWrite(4, 0xff);
  delay(500);
  ledcWrite(1, 0);
  ledcWrite(2, 0);
  ledcWrite(3, 0);
  ledcWrite(4, 0);
  //---Status LEDs config end

  //---touch config
  uint16_t touchReadVal;

  touchReadVal = touchMax(TOUCH_PIN1);
  Serial.print("TOUCH_PIN1:");
  Serial.println(touchReadVal);
  touchAttachInterrupt(TOUCH_PIN1, interruptTouch1, touchReadVal * THRESHOLD_RATIO);

  touchReadVal = touchMax(TOUCH_PIN2);
  Serial.print("TOUCH_PIN2:");
  Serial.println(touchReadVal);
  touchAttachInterrupt(TOUCH_PIN2, interruptTouch2, touchReadVal * THRESHOLD_RATIO);

  touchReadVal = touchMax(TOUCH_PIN3);
  Serial.print("TOUCH_PIN3:");
  Serial.println(touchReadVal);
  touchAttachInterrupt(TOUCH_PIN3, interruptTouch3, touchReadVal * THRESHOLD_RATIO);

  touchReadVal = touchMax(TOUCH_PIN4);
  Serial.print("TOUCH_PIN4:");
  Serial.println(touchReadVal);
  touchAttachInterrupt(TOUCH_PIN4, interruptTouch4, touchReadVal * THRESHOLD_RATIO);
  //---touch config end

  // We start by connecting to a WiFi network
  if (!SPIFFS.begin(true)) {
    Serial.print("SPIFFS Mount failed");
  }
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(led_gpio, OUTPUT);
  ///setup identity gpio
  hap_set_identity_gpio(identity_led);    //identity_led=2 will blink on identity

  /// now will setup homekit device

  //this is for custom storaage usage
  // In given example we are using \pair.dat   file in our spiffs system
  //see implementation below

  //SPIFFS.remove(pair_file_name);//reset pair_file
  init_hap_storage();
  set_callback_storage_change(storage_changed);

  /// We will use for this example only one accessory (possible to use a several on the same esp)
  //Our accessory type is light bulb , apple interface will proper show that
  hap_setbase_accessorytype(homekit_accessory_category_lightbulb);
  /// init base properties
  hap_initbase_accessory_service("host", "k-omura", "0", "WoodBox", "1.0");

  //we will add only one light bulb service and keep pointer for nest using
  hapservice = hap_add_relaydim_service("Led", led_callback, (void*)&led_gpio);

  //and finally init HAP
  hap_init_homekit_server();
}

void loop() {
  uint8_t axdl345IntSource = 0;
  uint8_t axdl345TapStatus = 0;

  uint8_t currentDuty = controlDuty();

  if (INT1Rised || INT2Rised) {
    INT1Rised = 0;
    INT2Rised = 0;

    Wire.beginTransmission(DEVICE_ADDRESS);
    Wire.write(0x30); //INT_SOURCE
    Wire.endTransmission();
    Wire.requestFrom(DEVICE_ADDRESS, (uint8_t)1);
    while (Wire.available()) {
      axdl345IntSource = Wire.read();
    }

    Wire.beginTransmission(DEVICE_ADDRESS);
    Wire.write(0x2b); //ACT_TAP_STATUS
    Wire.endTransmission();
    Wire.requestFrom(DEVICE_ADDRESS, (uint8_t)1);
    while (Wire.available()) {
      axdl345TapStatus = Wire.read();
    }

    if (axdl345TapStatus & 0b00000001) {
      Serial.print("x ");
    } else if (axdl345TapStatus & 0b00000010) {
      Serial.print("y ");
    } else if (axdl345TapStatus & 0b00000100) {
      Serial.print("z ");
    }

    if (axdl345IntSource & 0b01000000) {
      Serial.println("single tap");
    }
    if (axdl345IntSource & 0b00100000) {
      Serial.println("--->double tap");

      if (targetDuty == 0) {
        set_led(1);
      } else {
        set_led(0);
      }
    }
  }

  if (touchedPIN) {
    Serial.print("status=");
    Serial.println(touchedPIN);

    if (targetDuty == 0) {
      set_led(1);
    }

    switch (touchedPIN) {
      case 1:
        set_led_level(30);
        break;
      case 2:
        set_led_level(50);
        break;
      case 3:
        set_led_level(80);
        break;
      case 4:
        set_led_level(100);
        break;
      default:
        set_led_level(0);
        break;
    }
    controlStatusLEDs(touchedPIN);
    touchedPIN = 0;
  }
}

void init_hap_storage() {
  Serial.print("init_hap_storage");

  File fsDAT = SPIFFS.open(pair_file_name, "r");
  if (!fsDAT) {
    Serial.println("Failed to read pair.dat");
    return;
  }
  int size = hap_get_storage_size_ex();
  char* buf = new char[size];
  memset(buf, 0xff, size);
  int readed = fsDAT.readBytes(buf, size);
  Serial.print("Readed bytes ->");
  Serial.println(readed);
  hap_init_storage_ex(buf, size);
  fsDAT.close();
  delete []buf;
}

void storage_changed(char * szstorage, int size) {
  Serial.print("storage_changed");

  SPIFFS.remove(pair_file_name);
  File fsDAT = SPIFFS.open(pair_file_name, "w+");
  if (!fsDAT) {
    Serial.println("Failed to open pair.dat");
    return;
  }
  fsDAT.write((uint8_t*)szstorage, size);

  fsDAT.close();
}

//can be used for any logic, it will automatically inform Apple about state changes
void set_led(bool val) {
  Serial.print("set_led:");
  Serial.println(val);

  if (!val) {
    targetDuty = 0;
  }
  //we need notify apple about changes

  if (hapservice) {
    Serial.println("notify hap");
    //getting on/off characteristic
    homekit_characteristic_t * ch = homekit_service_characteristic_by_type(hapservice, HOMEKIT_CHARACTERISTIC_ON);
    if (ch) {
      Serial.println("found characteristic");
      if (ch->value.bool_value != val) { //wil notify only if different
        ch->value.bool_value = val;
        homekit_characteristic_notify(ch, ch->value);
      }
    }

    if (val) {
      targetDuty = dutyBeforeOff;

      Serial.println("notify led level:" + String(val));
      //getting on/off characteristic
      homekit_characteristic_t * ch = homekit_service_characteristic_by_type(hapservice, HOMEKIT_CHARACTERISTIC_BRIGHTNESS);
      HAP_NOTIFY_CHANGES(int, ch, targetDuty, 0);
    }
  }

}

void set_led_level(uint8_t val) {
  Serial.print("set_led_level:");
  Serial.println(val);

  targetDuty = val;
  if (val > 0) {
    dutyBeforeOff = targetDuty;
  }

  if (hapservice) {
    Serial.println("notify led level:" + String(val));
    //getting on/off characteristic
    homekit_characteristic_t * ch = homekit_service_characteristic_by_type(hapservice, HOMEKIT_CHARACTERISTIC_BRIGHTNESS);
    HAP_NOTIFY_CHANGES(int, ch, val, 0);
  }
}

void led_callback(homekit_characteristic_t *ch, homekit_value_t value, void *context) {
  Serial.println("led_callback");

  if (strcmp(ch->type, HOMEKIT_CHARACTERISTIC_ON) == 0) {
    set_led(ch->value.bool_value);
  } else if (strcmp(ch->type, HOMEKIT_CHARACTERISTIC_BRIGHTNESS) == 0) {
    set_led_level(ch->value.int_value);
  } else {
    Serial.println("unknown charactheristic");
  }
}
