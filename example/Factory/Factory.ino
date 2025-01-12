
#include "Arduino.h"
#include "SD_MMC.h"
#include "WiFi.h"
#include "logo.h"
#include "lv_driver.h"
#include "pin_config.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"

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

static char serial_buffer[255] = { 0 };
static int serial_buffer_idx = 0;
static bool serial_buffer_initialized = false;

TFT_eSPI tft = TFT_eSPI();
TaskHandle_t ledTaskHandle;
CRGB leds;
OneButton button(BTN_PIN, true);
uint8_t btn_press = 0;
int rotation = 1;
bool msc_initialized = false;
bool tft_backlight_on = true;
bool leds_on = true;
bool leds_run = true;
lv_obj_t *tv;
#define PRINT_STR(str, x, y)                                                                                                                         \
  do {                                                                                                                                               \
    Serial0.println(str);                                                                                                                             \
    tft.drawString(str, x, y);                                                                                                                       \
    y += 8;                                                                                                                                          \
  } while (0);

void led_task(void *param) {
  while (1) {
    if (leds_on) {
    static uint8_t hue = 0;
    if(leds_run) leds = CHSV(hue++, 0XFF, 100);
    FastLED.show();
    delay(50);
    } else {
      //leds = CHSV(0, 0xFF, 0);
      //FastLED.clear(true);
      leds[0] = CRGB::Black;
      FastLED.show();
    }
  }
  delay(50);
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
  Serial0.println("scan start");
  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial0.println("scan done");
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
      delay(1);
    }
  }
  Serial0.println("");
  
  x = y = 0;
  tft.fillScreen(TFT_BLACK);

  PRINT_STR("Connecting to:", x, y);
  PRINT_STR("Sloth Country Manor", x, y);
  WiFi.begin("Sloth Country Manor", "SlothLovesYou");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    button.tick();
    Serial.print(".");
  }
  // Print local IP address and start web server
  PRINT_STR("", x, y);
  PRINT_STR("WiFi connected.", x, y);
  PRINT_STR("", x, y);
  PRINT_STR("IP address: ", x, y);
  PRINT_STR(WiFi.localIP().toString(), x, y);
  Serial0.println();
}

void startMSC() {
  if (!msc_initialized) {
    Serial0.println("Initializing MSC");
    // Initialize USB metadata and callbacks for MSC (Mass Storage Class)
    msc.vendorID("ESP32");
    msc.productID("USB_MSC");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(true);
    msc.begin(SD_MMC.numSectors(), SD_MMC.sectorSize());

    Serial0.println("Initializing USB");

    USB.begin();
    USB.onEvent(usbEventCallback);

    msc_initialized = true;
  }

  return;
}

void stopMSC() {
  if (msc_initialized) {

    Serial0.println("Stopping MSC");
    msc.end();

    //Serial0.println("Stopping USB");
    //USB.end();

    msc_initialized = false;
  }
}

// Handler function for MultiClick the button with self pointer as a parameter
static void handleMultiClick(void *oneButton) {
  //Serial0.println("MultiClick numberClicks=%d!", oneButton->getNumberClicks());
  if (button.getNumberClicks() == 3) {
    if (tft_backlight_on) {
      backlightOff();
    } else {
      backlightOn();
    }
  } else if (button.getNumberClicks() == 5) {
    rebootDevice();
  } else if (button.getNumberClicks() == 6) {
    downloadMode();
  }
}

void backlightOff() {
  digitalWrite(TFT_LEDA_PIN, 1);
  tft_backlight_on = false;
  
  return;
}

void backlightOn() {
  digitalWrite(TFT_LEDA_PIN, 0);
  tft_backlight_on = true;

  return;
}

void rebootDevice() {
  stopMSC();
  ESP.restart();
}

void downloadMode() {
  leds_run = false;
  leds = CRGB::Green;
  tft.fillScreen(TFT_BLACK);
  tft.drawCentreString("WAITING FOR DOWNLOAD",80,36,1);
  leds_on = false;
  delay(500);
  stopMSC();
  vTaskDelete(ledTaskHandle);
  delay(100);
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  //ESP.deepSleep(2000000);
  ESP.restart();
}
void powerOff() {
    tft.fillScreen(TFT_BLACK);
    leds_run = false;
    leds = CRGB::Red;
    delay(500);
    leds_on = false;
    digitalWrite(TFT_LEDA_PIN, 1);
    tft_backlight_on = false;
    delay(100);
    stopMSC();
    vTaskDelete(ledTaskHandle);
    delay(100);
//    digitalWrite(LED_DI_PIN, 0);
//    digitalWrite(LED_CI_PIN, 0);
    pinMode(TFT_LEDA_PIN, INPUT_PULLUP);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_PIN,LOW); 
    esp_deep_sleep_start();
}


void setup() {
  int32_t x, y;
  
  leds_run = false;
  leds = CRGB::Green;

  Serial0.begin(115200);
  delay(200);
  
  Serial0.println();
  Serial0.println("████████╗   ██████╗  ██████╗ ███╗   ██╗ ██████╗ ██╗     ███████╗    ███████╗██████╗ ");
  Serial0.println("╚══██╔══╝   ██╔══██╗██╔═══██╗████╗  ██║██╔════╝ ██║     ██╔════╝    ██╔════╝╚════██╗");
  Serial0.println("   ██║█████╗██║  ██║██║   ██║██╔██╗ ██║██║  ███╗██║     █████╗█████╗███████╗ █████╔╝");
  Serial0.println("   ██║╚════╝██║  ██║██║   ██║██║╚██╗██║██║   ██║██║     ██╔══╝╚════╝╚════██║ ╚═══██╗");
  Serial0.println("   ██║      ██████╔╝╚██████╔╝██║ ╚████║╚██████╔╝███████╗███████╗    ███████║██████╔╝");
  Serial0.println("   ╚═╝      ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝ ╚══════╝╚══════╝    ╚══════╝╚═════╝ ");
  Serial0.println();
  Serial0.println("Hello from T-Dongle-S3!");
  Serial0.println();
  pinMode(TFT_LEDA_PIN, OUTPUT);

  // BGR ordering is typical
  FastLED.addLeds<APA102, LED_DI_PIN, LED_CI_PIN, BGR>(&leds, 1);

  xTaskCreatePinnedToCore(led_task, "led_task", 1024, NULL, 1, &ledTaskHandle, 0);

  // Initialise TFT
  tft.init();
  tft.setRotation(rotation);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(TFT_LEDA_PIN, 0);
  tft.setTextFont(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.pushImage(0, 0, 160, 80, (uint16_t *)gImage_logo);

  delay(1000);

  button.attachClick([] {
    startMSC();
  });

  button.attachDoubleClick([] {
    stopMSC();
  });

  button.attachLongPressStop([] {
    if (rotation == 1) {
      rotation = 3;
    } else {
      rotation = 1;
    }
    tft.setRotation(rotation);
  });

  // MultiClick button event attachment with self pointer as a parameter
  button.attachMultiClick(handleMultiClick, &button);

  button.tick();
  
  //Serial0.println("Mounting SDcard");
  x = y = 0;
  tft.fillScreen(TFT_BLACK);
  PRINT_STR("Mounting SDcard", x, y);
  sd_init(x, y);

  Serial0.printf("Card Size: %lluMB\r\n", SD_MMC.totalBytes() / 1024 / 1024);
  Serial0.printf("Sector: %d\tCount: %d\r\n", SD_MMC.sectorSize(), SD_MMC.numSectors());
  
  for(int i = 0; i < 10; ++i) {
    delay(100);
    button.tick();
  }
  //delay(1000);
  x = y = 0;
  tft.fillScreen(TFT_BLACK);

  File motdFile = SD_MMC.open("/motd");
  if ( motdFile ) {
    if (motdFile.available()) Serial0.println();
    String readString = "";
    while ( motdFile.available() ) {
      readString = motdFile.readStringUntil('\n');
      PRINT_STR(readString, x, y)
    }
    motdFile.close();
    Serial0.println();
    for(int i = 0; i < 30; ++i) {
      delay(100);
      button.tick();
    }
  }

  //delay(3000);
  x = y = 0;
  tft.fillScreen(TFT_BLACK);
  PRINT_STR("Scanning WiFi...", x, y);
  scan_wifi_rssi(x, y);
  for(int i = 0; i < 40; ++i) {
    delay(100);
    button.tick();
  }
  //delay(4000);

  button.attachClick([] {
    btn_press = 1 ^ btn_press;
    lv_obj_set_tile_id(tv, 0, btn_press, LV_ANIM_ON);
    startMSC();
  });

  button.attachDoubleClick([] {
    stopMSC();
  });

  lvgl_init();

  // Show CC CE
  lv_obj_t * img_cc = lv_img_create(lv_scr_act());
  lv_img_set_src(img_cc, &image_logo);
  lv_obj_center(img_cc);
  int i = 250;
  while (i--) {
    lv_task_handler(); delay(5);
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

  leds_run = true;
  Serial0.print("> ");
}

void loop() { // Put your main code here, to run repeatedly:
  lv_timer_handler();
  button.tick();
  checkIfShouldPowerOff();
  
  String input = getSerialInput();

  if (input != "" && input != "\n") {
    Serial0.println();
    Serial0.print("Received Command: ");
    Serial0.println(input);
    if(input.length() > 0) {
      processCommand(input);
    }
    Serial0.print("> ");
  }
  
  delay(1);
}

#if !SOC_USB_OTG_SUPPORTED || ARDUINO_USB_MODE
#error Device does not support USB_OTG or native USB CDC/JTAG is selected
#endif

//int clk = 36;
//int cmd = 35;
//int d0 = 37;
//int d1 = 38;
//int d2 = 33;
//int d3 = 34;
bool onebit = false;  // set to false for 4-bit. 1-bit will ignore the d1-d3 pins (but d3 must be pulled high)

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
      case ARDUINO_USB_STARTED_EVENT: Serial0.println("USB PLUGGED"); break;
      case ARDUINO_USB_STOPPED_EVENT: Serial0.println("USB UNPLUGGED"); break;
      case ARDUINO_USB_SUSPEND_EVENT: Serial0.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en); break;
      case ARDUINO_USB_RESUME_EVENT:  Serial0.println("USB RESUMED"); break;

      default: break;
    }
  }
}

void checkIfShouldPowerOff() {
      int countDown = 3;
    /* Long press power off */
    if (button.isLongPressed())
    {
        button.tick();
        uint32_t time_count = millis();
        while (button.isLongPressed())
        {
          button.tick();
          // Display poweroff bar only if holding button
          tft.setTextSize(1);
          tft.setTextColor(TFT_GREEN, TFT_BLACK);
          countDown = 3 - (millis() - time_count) / 1000;
          if(countDown>0) tft.drawCentreString("POWERING OFF IN "+String(countDown)+"...",80,32,1);
          else { 
            tft.fillScreen(TFT_BLACK);
            tft.drawCentreString("GOODBYE!",80,36,1);
            while(button.isLongPressed()) { button.tick(); delay(1); }
            powerOff();
          }
          delay(10);
        }

        // Clear text after releasing the button
        delay(10);
        tft.fillRect(72, 36, 160 - 72, tft.fontHeight(1), TFT_BLACK);
    }
}

String getSerialInput() {
  int bytes_received = 0;
  int bytes_available = 0;
  String command_line = "";
  char command_buffer[255];
  
  memset(command_buffer, '\0', 255);

  if (Serial0.available() > 0 && serial_buffer_idx < 255) {
    bytes_available = Serial0.available();
    //Serial.println("Should have bytes available: " + bytes_available);
    int bytes_to_read = bytes_available < (254 - serial_buffer_idx) ? bytes_available : (254 - serial_buffer_idx);
    bytes_received = Serial0.readBytes(&serial_buffer[serial_buffer_idx], bytes_to_read);

    // Echo what we just got to the terminal
    for (int i = serial_buffer_idx; i < serial_buffer_idx + bytes_received; i++) {
      // Support backspace
      if (serial_buffer[i] == '\x7f') {
        // j starts at i + 1 which is the next
        // valid character. We skip copying the
        // backspace Ox7f character.
        for (int j = i + 1; j < 255; j++) {
          if (j <= 1) {
            break;
          } else if (j == 254 || j > serial_buffer_idx + bytes_received) {
            serial_buffer[j] = '\0';
          } else {
            serial_buffer[j-2] = serial_buffer[j];
          }
        }
        if (serial_buffer_idx > 0) {
          Serial0.print('\b');
          Serial0.print(' ');
          Serial0.print('\b');
        }
        // This is kinda weird because we decrement serial_buffer_idx ...
        // there's a better way to do this I'm sure. TODO(jstockdale): Fix me!
        serial_buffer_idx = (serial_buffer_idx >= 1) ? serial_buffer_idx - 2 : (bytes_received == 1) ? -1 : 0;
        // send cursor back on serial console, blank previous character, send cursor back once more
      }
      // Skip printing \n or \r to the terminal
      if (serial_buffer[i] != '\n' && serial_buffer[i] != '\r') {
        Serial0.print(serial_buffer[i]);
      }
    }

    serial_buffer_idx += bytes_received;
    
    if (serial_buffer_idx < 254) {
      serial_buffer[serial_buffer_idx+1] = '\0';
    } else if (serial_buffer_idx >= 254) {
      Serial0.println("Serial buffer overrun?");
    }
//    Serial0.println("Received bytes over serial: " + String(bytes_received));
//    Serial0.println("Buffer: " + String(serial_buffer));
//    Serial0.println("Buffer idx: " + String(serial_buffer_idx));
  }

  int index_of_newline = -1;

  for (int i = 0; i < serial_buffer_idx; i++) {
    
    // This is mind boggling that we sometimes
    // just get \r and not \n and also not \r\n ... 
    // but hey. why not. :D I inspected the hex values
    // and this is definitely what's coming over the line.
    // We try to handle all of the possible combinations.
    if (serial_buffer[i] == '\n' || serial_buffer[i] == '\r') {
      //Serial0.println("Found \"newline\" at index: " + String(i));
      index_of_newline = i;
      if (i == 0) {
        command_buffer[0] = '\n';
      }
    } else if (i == 254) {
        command_buffer[i] = '\0';
    } else {
      command_buffer[i] = serial_buffer[i];
    }
  }
  
  command_line = command_buffer;

  if (index_of_newline > -1) {
    // if we got a \r with a \n, treat the \n as the correct newline
    if (serial_buffer[index_of_newline] == '\r' && serial_buffer[index_of_newline+1] == '\n') {
      index_of_newline++;
    }
    // Move the unconsumed portion of the buffer over
    // and zero extra bytes.
    for (int i = index_of_newline + 1; i < 255; i++) {
      // Copy any characters we have after the newline, if they exist
      serial_buffer[i - (index_of_newline + 1)] = serial_buffer[i];
      serial_buffer[i] = '\0';
    }

    // make sure to set correct buffer index for remaining data
    serial_buffer_idx = serial_buffer_idx - (index_of_newline + 1);

    command_line.trim();
    return command_line;
  } else if (index_of_newline == -1 && serial_buffer_idx == 254) {
    // flush buffer if we're full; don't let it overrun
    Serial0.println("Serial buffer full! Processing command and flushing buffer.");
    serial_buffer_idx = 0;
    for (int i = 0; i < 255; ++i) {
      serial_buffer[i] = '\0';
    }
    command_line.trim();
    return command_line;
  } else {
    return "";
  }
}

int processCommand(String input) {
  if(input.length() > 0) {
    if (input == "poweroff") {
      Serial0.println("Powering off!");
      powerOff();
    } else if (input == "reboot") {
      Serial0.println("Rebooting device!");
      rebootDevice();
    } else if (input == "downloadmode") {
      Serial0.println("Entering download mode ...");
      downloadMode();
    } else if (input == "sdinfo" ) {
      sdInfo();
    } else if (input == "startmsc") {
      startMSC();
    } else if (input == "stopmsc") {
      stopMSC();
    } else if (input == "help") {
      displayHelp();
    } else {
      Serial0.println("\r\nDid not recognize command. Type \"help\" ...");
    }
  }
  return 0;
}

void displayHelp() {
  Serial0.println("\n\rAvailable commands: poweroff | reboot | downloadmode | sdinfo | startmsc | stopmsc");
}

void sdInfo() {
  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial0.println("No SD_MMC card attached");
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

  Serial0.println(str);
  uint32_t cardSize = SD_MMC.cardSize() / (1024 * 1024);

  str = "SD_MMC Card Size: ";
  str += cardSize;
  Serial0.println(str);

  str = "Total space: ";
  str += uint32_t(SD_MMC.totalBytes() / (1024 * 1024));
  str += "MB";
  Serial0.println(str);

  str = "Used space: ";
  str += uint32_t(SD_MMC.usedBytes() / (1024 * 1024));
  str += "MB";
  Serial0.println(str);
}
