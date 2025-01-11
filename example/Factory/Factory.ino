
#include "Arduino.h"
#include "SD_MMC.h"
#include "WiFi.h"
#include "logo.h"
#include "lv_driver.h"
#include "pin_config.h"
#include "esp_chip_info.h"

/* external library */
/* To use Arduino, you need to place lv_conf.h in the \Arduino\libraries directory */
#include "OneButton.h" // https://github.com/mathertel/OneButton
#include "TFT_eSPI.h"  // https://github.com/Bodmer/TFT_eSPI

#include "lv_conf.h"
#include "lvgl.h"    // https://github.com/lvgl/lvgl
#include <FastLED.h> // https://github.com/FastLED/FastLED

#include <USB.h>
#include <USBMSC.h>

// USB Mass Storage Class (MSC) object
USBMSC msc;

LV_IMG_DECLARE(image_logo);


TFT_eSPI tft = TFT_eSPI();
CRGB leds;
OneButton button(BTN_PIN, true);
uint8_t btn_press = 0;
int rotation = 1;
bool msc_initialized = false;
lv_obj_t *tv;
#define PRINT_STR(str, x, y)                                                                                                                         \
  do {                                                                                                                                               \
    Serial.println(str);                                                                                                                             \
    tft.drawString(str, x, y);                                                                                                                       \
    y += 8;                                                                                                                                          \
  } while (0);

void led_task(void *param) {
  while (1) {
    static uint8_t hue = 0;
    leds = CHSV(hue++, 0XFF, 100);
    FastLED.show();
    delay(50);
  }
}

void sd_init(int32_t &x, int32_t &y) {
  //int32_t x, y;
  SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN, SD_MMC_D1_PIN, SD_MMC_D2_PIN, SD_MMC_D3_PIN);
  if (!SD_MMC.begin("/sdcard", false)) {
    PRINT_STR("Card Mount Failed", x, y)
    return;
  }
  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    PRINT_STR("No SD_MMC card attached", x, y)
    return;
  }
  String str;
  str = "SD_MMC Card Type: ";
  if (cardType == CARD_MMC) {
    str += "MMC";
  } else if (cardType == CARD_SD) {
    str += "SD_MMCSC";
  } else if (cardType == CARD_SDHC) {
    str += "SD_MMCHC";
  } else {
    str += "UNKNOWN";
  }

  PRINT_STR(str, x, y)
  uint32_t cardSize = SD_MMC.cardSize() / (1024 * 1024);

  str = "SD_MMC Card Size: ";
  str += cardSize;
  PRINT_STR(str, x, y)

  str = "Total space: ";
  str += uint32_t(SD_MMC.totalBytes() / (1024 * 1024));
  str += "MB";
  PRINT_STR(str, x, y)

  str = "Used space: ";
  str += uint32_t(SD_MMC.usedBytes() / (1024 * 1024));
  str += "MB";
  PRINT_STR(str, x, y)
}

void scan_wifi_rssi(int32_t &x, int32_t &y) {
  String str = "";
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("scan start");
  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  x = y = 0;
  if (n == 0) {
    PRINT_STR("no networks found", x, y);
  } else {
    str = "";
    str += n;
    str += " networks found";
    PRINT_STR(str, x, y);
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      str = "";
      str += (i + 1);
      str += ": ";
      str += WiFi.SSID(i);
      str += " (";
      str += WiFi.RSSI(i);
      str += ") ";
      str += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*";
      PRINT_STR(str, x, y);
      delay(5);
    }
  }
  Serial.println("");
  WiFi.mode(WIFI_OFF);
}

void startMSC() {
  if(!msc_initialized) {
    Serial.println("Initializing MSC");
    // Initialize USB metadata and callbacks for MSC (Mass Storage Class)
    msc.vendorID("ESP32");
    msc.productID("USB_MSC");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    msc.begin(SD_MMC.numSectors(), SD_MMC.sectorSize());
  
    Serial.println("Initializing USB");
  
    USB.begin();
    USB.onEvent(usbEventCallback);

    msc_initialized = true;
  }
  
  return;
}

void stopMSC() {
  if(msc_initialized) {
    
    Serial.println("Stopping MSC");
    msc.end();
    
    //Serial.println("Stopping USB");
    //USB.end();

    msc_initialized = false;
  }
}

void setup() {
  int32_t x, y;
  
  Serial.begin(115200);
  delay(200);
  Serial.println("Hello from T-Dongle-S3!");
  pinMode(TFT_LEDA_PIN, OUTPUT);

  // Initialise TFT
  tft.init();
  tft.setRotation(rotation);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(TFT_LEDA_PIN, 0);
  tft.setTextFont(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.pushImage(0, 0, 160, 80, (uint16_t *)gImage_logo);
  delay(500);

  //Serial.println("Mounting SDcard");
  PRINT_STR("Mounting SDcard", x, y);
  sd_init(x, y);
  
  Serial.printf("Card Size: %lluMB\n", SD_MMC.totalBytes() / 1024 / 1024);
  Serial.printf("Sector: %d\tCount: %d\n", SD_MMC.sectorSize(), SD_MMC.numSectors());
  
  delay(500);
  x = y = 0;
  tft.fillScreen(TFT_BLACK);

  File motdFile = SD_MMC.open("/motd");
  if( motdFile ) {
    String readString = "";
    while( motdFile.available() ) {
      readString = motdFile.readStringUntil('\n'); 
      PRINT_STR(readString, x, y)
    }
    motdFile.close();
  }
  delay(3000);

  //delay(500);
  x = y = 0;
  tft.fillScreen(TFT_BLACK);
  PRINT_STR("Scanning WiFi...", x, y);
  scan_wifi_rssi(x, y);
  delay(4000);
  // BGR ordering is typical
  FastLED.addLeds<APA102, LED_DI_PIN, LED_CI_PIN, BGR>(&leds, 1);

  button.attachClick([] {
    btn_press = 1 ^ btn_press;
    lv_obj_set_tile_id(tv, 0, btn_press, LV_ANIM_ON);
    startMSC();
  });

  button.attachDoubleClick([] {
    stopMSC();
  });

  button.attachLongPressStart([] {
    if(rotation == 1) {
      rotation = 3;
    } else {
      rotation = 1;
    }
    tft.setRotation(rotation);
  });

  xTaskCreatePinnedToCore(led_task, "led_task", 1024, NULL, 1, NULL, 0);

  lvgl_init();

  // Show CC CE
  lv_obj_t * img_cc = lv_img_create(lv_scr_act());
  lv_img_set_src(img_cc,&image_logo);
  lv_obj_center(img_cc);
  int i = 500;
  while(i--){
    lv_task_handler();delay(5);
  }
  lv_obj_del(img_cc);
  // End

  tv = lv_tileview_create(lv_scr_act());
  lv_obj_remove_style(tv, NULL, LV_PART_SCROLLBAR);

  /* just show the logo gif */
  lv_obj_t *tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_VER);
  LV_IMG_DECLARE(nyan_cat_synthwave);
  //LV_IMG_DECLARE(img_src_logo);
  lv_obj_t *img = lv_gif_create(tile1);
  lv_obj_set_size(img, 160, 80);
  lv_gif_set_src(img, &nyan_cat_synthwave);
  lv_obj_center(img);

  lv_obj_t *tile2 = lv_tileview_add_tile(tv, 0, 1, LV_DIR_VER);
  lv_obj_t *label = lv_label_create(tile2);
  String text;
  esp_chip_info_t t;
  esp_chip_info(&t);
  text = "Thumbdrive Mode\n";
  text += "by @jstockdale";
  lv_label_set_text(label, text.c_str());
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

void loop() { // Put your main code here, to run repeatedly:
  lv_timer_handler();
  button.tick();
  delay(5);
}

#if !SOC_USB_OTG_SUPPORTED || ARDUINO_USB_MODE
#error Device does not support USB_OTG or native USB CDC/JTAG is selected
#endif

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize) {
    return false;  // disk error
  }
  log_v("Write lba: %ld\toffset: %ld\tbufsize: %ld", lba, offset, bufsize);
  for (int x = 0; x < bufsize / secSize; x++) {
    uint8_t blkbuffer[secSize];
    memcpy(blkbuffer, (uint8_t *)buffer + secSize * x, secSize);
    if (!SD_MMC.writeRAW(blkbuffer, lba + x)) {
      return false;
    }
  }
  return bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize) {
    return false;  // disk error
  }
  log_v("Read lba: %ld\toffset: %ld\tbufsize: %ld\tsector: %lu", lba, offset, bufsize, secSize);
  for (int x = 0; x < bufsize / secSize; x++) {
    if (!SD_MMC.readRAW((uint8_t *)buffer + (x * secSize), lba + x)) {
      return false;  // outside of volume boundary
    }
  }
  return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  log_i("Start/Stop power: %u\tstart: %d\teject: %d", power_condition, start, load_eject);
  return true;
}

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == ARDUINO_USB_EVENTS) {
    arduino_usb_event_data_t *data = (arduino_usb_event_data_t *)event_data;
    switch (event_id) {
      case ARDUINO_USB_STARTED_EVENT: Serial.println("USB PLUGGED"); break;
      case ARDUINO_USB_STOPPED_EVENT: Serial.println("USB UNPLUGGED"); break;
      case ARDUINO_USB_SUSPEND_EVENT: Serial.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en); break;
      case ARDUINO_USB_RESUME_EVENT:  Serial.println("USB RESUMED"); break;

      default: break;
    }
  }
}
