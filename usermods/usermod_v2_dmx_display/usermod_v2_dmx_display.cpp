#include "usermod_v2_dmx_display.h"
#include "dmx_display_wled_fonts.h"

#ifdef ARDUINO_ARCH_ESP32
static TaskHandle_t Display_Task = nullptr;
void DisplayTaskCode(void * parameter);
#endif

// strings to reduce flash memory usage (used more than twice)
const char DMXDisplay::_name[]            PROGMEM = "DMXDisplay";
const char DMXDisplay::_enabled[]         PROGMEM = "enabled";
const char DMXDisplay::_contrast[]        PROGMEM = "contrast";
const char DMXDisplay::_refreshRate[]     PROGMEM = "refreshRate-ms";
const char DMXDisplay::_screenTimeOut[]   PROGMEM = "screenTimeOutSec";
const char DMXDisplay::_flip[]            PROGMEM = "flip";
const char DMXDisplay::_sleepMode[]       PROGMEM = "sleepMode";
const char DMXDisplay::_busClkFrequency[] PROGMEM = "i2c-freq-kHz";
const char DMXDisplay::_contrastFix[]     PROGMEM = "contrastFix";

bool apActiveBool = false;

#if defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS)
DMXDisplay *DMXDisplay::instance = nullptr;
#endif

// some displays need this to properly apply contrast
void DMXDisplay::setVcomh(bool highContrast) {
  if (type == NONE || !enabled) return;
  u8x8_t *u8x8_struct = u8x8->getU8x8();
  u8x8_cad_StartTransfer(u8x8_struct);
  u8x8_cad_SendCmd(u8x8_struct, 0x0db); //address of value
  u8x8_cad_SendArg(u8x8_struct, highContrast ? 0x000 : 0x040); //value 0 for fix, reboot resets default back to 64
  u8x8_cad_EndTransfer(u8x8_struct);
}

void DMXDisplay::startDisplay() {
  if (type == NONE || !enabled) return;
  lineHeight = u8x8->getRows() > 4 ? 2 : 1;
  DEBUG_PRINTLN(F("Starting display."));
  u8x8->setBusClock(ioFrequency);  // can be used for SPI too
  u8x8->begin();
  setFlipMode(flip);
  setVcomh(contrastFix);
  setContrast(contrast); //Contrast setup will help to preserve OLED lifetime. In case OLED need to be brighter increase number up to 255
  setPowerSave(0);
  setMode(MODE_NET);
}

/**
 * Wrappers for screen drawing
 */
void DMXDisplay::setFlipMode(uint8_t mode) {
  if (type == NONE || !enabled) return;
  u8x8->setFlipMode(mode);
}
void DMXDisplay::setContrast(uint8_t contrast) {
  if (type == NONE || !enabled) return;
  u8x8->setContrast(contrast);
}
void DMXDisplay::drawString(uint8_t col, uint8_t row, const char *string, bool ignoreLH) {
  if (type == NONE || !enabled) return;
  drawing = true;
  u8x8->setFont(u8x8_font_chroma48medium8_r);
  if (!ignoreLH && lineHeight==2) u8x8->draw1x2String(col, row, string);
  else                            u8x8->drawString(col, row, string);
  drawing = false;
}
void DMXDisplay::draw2x2String(uint8_t col, uint8_t row, const char *string) {
  if (type == NONE || !enabled) return;
  drawing = true;
  u8x8->setFont(u8x8_font_chroma48medium8_r);
  u8x8->draw2x2String(col, row, string);
  drawing = false;
}
uint8_t DMXDisplay::getCols() {
  if (type==NONE || !enabled) return 0;
  return u8x8->getCols();
}
void DMXDisplay::clear() {
  if (type == NONE || !enabled) return;
  drawing = true;
  u8x8->clear();
  drawing = false;
}
void DMXDisplay::setPowerSave(uint8_t save) {
  if (type == NONE || !enabled) return;
  u8x8->setPowerSave(save);
}

void DMXDisplay::center(String &line, uint8_t width) {
  int len = line.length();
  if (len<width) for (unsigned i=(width-len)/2; i>0; i--) line = ' ' + line;
  for (unsigned i=line.length(); i<width; i++) line += ' ';
}

/**
 * Enable sleep (turn the display off) or clock mode.
 */
void DMXDisplay::sleepDisplay(bool enabled) {
  if (enabled) {
    displayTurnedOff = true;
      setPowerSave(1);
  } else {
    displayTurnedOff = false;
    setPowerSave(0);
  }
  currentMode = MODE_OFF;
}

// gets called once at boot. Do all initialization that doesn't depend on
// network here
void DMXDisplay::setup() {
  bool isSPI = (type == SSD1306_SPI || type == SSD1306_SPI64 || type == SSD1309_SPI64);

  // check if pins are -1 and disable usermod as PinManager::allocateMultiplePins() will accept -1 as a valid pin
  if (isSPI) {
    if (spi_sclk<0 || spi_mosi<0 || ioPin[0]<0 || ioPin[1]<0 || ioPin[1]<0) {
      type = NONE;
    } else {
      PinManagerPinType cspins[3] = { { ioPin[0], true }, { ioPin[1], true }, { ioPin[2], true } };
      if (!PinManager::allocateMultiplePins(cspins, 3, PinOwner::UM_DMXDisplay)) { type = NONE; }
    }
  } else {
    if (i2c_scl<0 || i2c_sda<0) { type=NONE; }
  }

  DEBUG_PRINTLN(F("Allocating display."));
  switch (type) {
    // U8X8 uses Wire (or Wire1 with 2ND constructor) and will use existing Wire properties (calls Wire.begin() though)
    case SSD1306:       u8x8 = (U8X8 *) new U8X8_SSD1306_128X32_UNIVISION_HW_I2C(); break;
    case SH1106:        u8x8 = (U8X8 *) new U8X8_SH1106_128X64_WINSTAR_HW_I2C();    break;
    case SSD1306_64:    u8x8 = (U8X8 *) new U8X8_SSD1306_128X64_NONAME_HW_I2C();    break;
    case SSD1305:       u8x8 = (U8X8 *) new U8X8_SSD1305_128X32_ADAFRUIT_HW_I2C();  break;
    case SSD1305_64:    u8x8 = (U8X8 *) new U8X8_SSD1305_128X64_ADAFRUIT_HW_I2C();  break;
    case SSD1309_64:    u8x8 = (U8X8 *) new U8X8_SSD1309_128X64_NONAME0_HW_I2C();   break;
    // U8X8 uses global SPI variable that is attached to VSPI bus on ESP32
    case SSD1306_SPI:   u8x8 = (U8X8 *) new U8X8_SSD1306_128X32_UNIVISION_4W_HW_SPI(ioPin[0], ioPin[1], ioPin[2]); break; // Pins are cs, dc, reset
    case SSD1306_SPI64: u8x8 = (U8X8 *) new U8X8_SSD1306_128X64_NONAME_4W_HW_SPI(ioPin[0], ioPin[1], ioPin[2]);    break; // Pins are cs, dc, reset
    case SSD1309_SPI64: u8x8 = (U8X8 *) new U8X8_SSD1309_128X64_NONAME0_4W_HW_SPI(ioPin[0], ioPin[1], ioPin[2]);   break; // Pins are cs, dc, reset
    // catchall
    default:            u8x8 = (U8X8 *) new U8X8_NULL(); enabled = false; break; // catchall to create U8x8 instance
  }

  if (nullptr == u8x8) {
    DEBUG_PRINTLN(F("Display init failed."));
    if (isSPI) {
      PinManager::deallocateMultiplePins((const uint8_t*)ioPin, 3, PinOwner::UM_DMXDisplay);
    }
    type = NONE;
    return;
  }

  startDisplay();
  onUpdateBegin(false);  // create Display task
  initDone = true;
}

// gets called every time WiFi is (re-)connected. Initialize own network
// interfaces here
void DMXDisplay::connected() {
  knownSsid = apActive ? apSSID : WiFi.SSID();
  // knownSsid = WiFi.SSID();
  knownIp   = Network.localIP(); 
  wakeDisplay();
  setMode(MODE_NET);
}

/**
 * Da loop.
 */
void DMXDisplay::loop() {
  // For non-threaded builds keep the original guard
  #if !(defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS))
    if (!enabled || strip.isUpdating()) return;
  #endif


    unsigned long now = millis();

    // ---- Button handling (active-low, INPUT_PULLUP) ----
    static bool buttonConfigured = false;
    static bool btnLastState = true;      // HIGH = unpressed (pull-up)
    static unsigned long btnPressStart = 0;
    static bool longPressHandled = false;

    // Configure pin once when available
    if (buttonPin >= 0 && !buttonConfigured) {
      pinMode(buttonPin, INPUT_PULLUP);
      buttonConfigured = true;
      btnLastState = digitalRead(buttonPin);  // stable initial state
    }

    // Read button (or treat as unpressed if not configured)
    bool reading = (buttonPin >= 0) ? digitalRead(buttonPin) : true;

    // Detect press edge (HIGH -> LOW)
    if (!reading && btnLastState) {
      btnPressStart = now;
    }

    // Detect long press during hold
    if (!reading && !btnLastState) {   // button still held
        unsigned long pressDuration = now - btnPressStart;

        if (pressDuration >= 2000 && !longPressHandled) {

            setMode(MODE_OFF);

            longPressHandled = true;   // prevent release from re-triggering
        }
    }


    // Detect release edge (LOW->HIGH)
    if (reading && !btnLastState) {

        unsigned long pressDuration = now - btnPressStart;

        // If long press already handled, ignore this release completely
        if (longPressHandled) {
            longPressHandled = false;   // Reset for next press
            btnLastState = reading;
            return;
        }

        // ---- SHORT PRESS ONLY ----
        if (pressDuration < 2000) {
            if (currentMode == MODE_OFF) {
                // Serial.println(F("[DMXDisplay] SHORT PRESS (wakeup) -> MODE_NET"));
                setMode(MODE_NET);
            } else {
                DisplayMode next = (currentMode == MODE_DMX) ? MODE_NET : MODE_DMX;
                // Serial.printf("[DMXDisplay] SHORT PRESS -> toggle to %s\n", next == MODE_DMX ? "DMX" : "NET");
                setMode(next);
            }
        }
    }



    // update last state for next loop iteration
    btnLastState = reading;


    // --- SSID-only scrolling refresh ---
    if (currentMode == MODE_NET) {
        static unsigned long lastSSIDRefresh = 0;
        int maxVisible = (getCols() / 2);  // each char is 2 cols wide


        // Only scroll if SSID actually overflows
        if (knownSsid.length() > 8) {

            if (millis() - lastSSIDRefresh > 300) {
                lastSSIDRefresh = millis();

                // Build padded scroll buffer so text doesn't merge
                static String scrollSrc;
                static bool built = false;

                if (!built) {
                    // 5 spaces padding looks clean on 128px width
                    scrollSrc = "     " + knownSsid + "     ";
                    built = true;
                }

                static uint8_t scrollIndex = 0;
                scrollIndex = (scrollIndex + 1) % scrollSrc.length();

                // Get rotated substring
                int len = scrollSrc.length();
                String shown;

                if (scrollIndex + maxVisible <= len) {
                    shown = scrollSrc.substring(scrollIndex, scrollIndex + maxVisible);
                } else {
                    // Wrap should only happen via padded string, not mid-word
                    int firstPart = len - scrollIndex;
                    shown = scrollSrc.substring(scrollIndex);
                    shown += scrollSrc.substring(0, maxVisible - firstPart);
                }

                // Center horizontally
                int col = (getCols() - shown.length() * 2) / 2;
                if (col < 0) col = 0;

                // ---- Clear ONLY the 2×2 text rows ----
                // big text row = 1 & 2 in your drawing layout
                for (int r = 1; r <= 2; r++) {
                    for (int c = 0; c < getCols(); c++) {
                        drawString(c, r, " "); // clear single cell
                    }
                }

                // Draw updated 2×2 SSID text
                draw2x2String(col, 1, shown.c_str());
            }
        }
    }



    // ---- Non-threaded redraw scheduling ----
  #if !(defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS))
    if (now < nextUpdate) return;
    nextUpdate = now + ((displayTurnedOff && clockMode && showSeconds) ? 1000 : refreshRate);
    redraw(false);
  #endif
}

/**
 * Redraw the screen (but only if things have changed
 * or if forceRedraw).
 */
void DMXDisplay::redraw(bool forceRedraw) {
  bool needRedraw = false;
  unsigned long now = millis();

  
  if (type == NONE || !enabled) return;

  while (drawing && millis()-now < 25) delay(1); // wait if someone else is drawing
  if (drawing || lockRedraw) return;

  if (apActive && WLED_WIFI_CONFIGURED && now<15000) {
    knownSsid = apSSID;
    return;
  }

  // Check if values which are shown on display changed from the last time.
  if (forceRedraw) {
    needRedraw = true;
    clear();
  } else if (wificonnected != interfacesInited) {   // WiFi state changed
    wificonnected = interfacesInited;
    wakeDisplay();

    if (currentMode == MODE_NET && !displayTurnedOff) {
      // If we're on the network screen, refresh the network info
      updateNetworkInfo();
      lastRedraw = now;
    }
    
    return;
  } else if (knownDMXAddress != DMXAddress && currentMode == MODE_DMX) {
      if (displayTurnedOff && nightlightActive) { knownDMXAddress = DMXAddress; }
      else if (!displayTurnedOff) { updateDMX(); lastRedraw = now; return; }
  } else if (knownDMXUniverse != e131Universe && currentMode == MODE_DMX) {
      if (displayTurnedOff && nightlightActive) { knownDMXUniverse = e131Universe; }
      else if (!displayTurnedOff) { updateDMX(); lastRedraw = now; return; }
  }

  if (!needRedraw) {
    // Nothing to change.
    // Turn off display after 1 minutes with no change.
    if (sleepMode && !displayTurnedOff && (millis() - lastRedraw > screenTimeout)) {
      // We will still check if there is a change in redraw()
      // and turn it back on if it changed.
      clear();
      sleepDisplay(true);
    }
    return;
  }

  lastRedraw = now;

  // Turn the display back on
  wakeDisplay();

  // Update last known values.
  knownDMXAddress      = DMXAddress;
  knownDMXUniverse     = e131Universe;
  apActiveBool         = Network.isConnected() ? false : true;
  knownSsid            = apActiveBool ? apSSID : WiFi.SSID();
  knownPassword        = apPass;
  knownIp              = Network.localIP();
  wificonnected        = interfacesInited;

  // Do the actual drawing
  if (!enabled || displayTurnedOff) return;

  switch (currentMode) {
    case MODE_DMX:
      updateDMX();
      break;

    case MODE_NET:
      updateNetworkInfo();
      break;

    case MODE_OFF:
      clear();
      setPowerSave(1);
      displayTurnedOff = true;
      break;
  }

  displayTurnedOff = (currentMode == MODE_OFF);

}

void DMXDisplay::updateDMX() {
#if defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS)
  unsigned long now = millis();
  while (drawing && millis() - now < 125) delay(1);
  if (drawing || lockRedraw) return;
#endif

  knownDMXAddress  = DMXAddress;
  knownDMXUniverse = e131Universe;

  lockRedraw = true;

  // ---------- Build DMX string ----------
  char dmxLine[16];
  snprintf(dmxLine, sizeof(dmxLine), "%d/%03d", e131Universe, DMXAddress);

  // ---------- Build title (serverDescription) ----------
  String title = serverDescription;

  // Clear the screen before redrawing
  clear();

  // ---------- TOP ROW: serverDescription ----------
  drawString(0, 0, serverDescription);

  String rightTitle = "DMX";
  int rtCol = getCols() - rightTitle.length();
  if (rtCol < 0) rtCol = 0;
  drawString(rtCol, 0, rightTitle.c_str());

  // ---------- DMX TEXT (true 2×2 large font) ----------
  int textLen = strlen(dmxLine);

  // Character width doubles for 2×2 font
  int col = (getCols() - (textLen * 2)) / 2;
  if (col < 0) col = 0;

  // Draw big DMX text — occupies row 2 & 3 automatically
  draw2x2String(col, 2, dmxLine);

  lockRedraw = false;
}

void DMXDisplay::updateNetworkInfo() {
  #if defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS)
    unsigned long now = millis();
    while (drawing && millis() - now < 125) delay(1);
    if (drawing || lockRedraw) return;
  #endif

  if (currentMode != MODE_NET) return;

  clear();

  lockRedraw = true;

  drawString(0, 0, serverDescription);

  if (Network.isConnected())
  {
    knownSsid = WiFi.SSID();
  }
  else if (apActive)
  {
    knownSsid = apSSID;
  }

  // AP mode active
  String rightTitle = Network.isConnected() ? "WiFi" : "AP";
  int rtCol = getCols() - rightTitle.length();
  if (rtCol < 0) rtCol = 0;
  drawString(rtCol, 0, rightTitle.c_str());
  
  String centerSSID = knownSsid;

  static unsigned long lastScroll = 0;
    static uint8_t index = 0;

    if (centerSSID.length() > 8 && millis() - lastScroll > 400) {
      index = (index + 1) % centerSSID.length();
      lastScroll = millis();
    }

    String shown = (centerSSID.length() > 8)
        ? centerSSID.substring(index) + " " + centerSSID.substring(0, index)
        : centerSSID;

    int col = (getCols() - shown.length() * 2) / 2;
    if (col < 0) col = 0;

    draw2x2String(col, 1, shown.c_str());

  if (wificonnected)
  {
    String centerIP = knownIp.toString();
    center(centerIP, getCols());
    drawString(0, 3, centerIP.c_str());
  }
  else
  {
    String apPassString = knownPassword;
    String centerPassword = apPassString;
    center(centerPassword, getCols());
    drawString(0, 3, centerPassword.c_str());
  }

  lockRedraw = false;
}



void DMXDisplay::setMode(DisplayMode m) {
  if (type == NONE || !enabled) return;

  currentMode = m;

  if (m == MODE_OFF) {
    // Fully blank and power-save the display
    displayTurnedOff = true;
    clear();
    setPowerSave(1);
  } else {
    // Ensure display is powered and marked as on
    displayTurnedOff = false;
    setPowerSave(0);
  }

  // Force an immediate full redraw of the new mode
  redraw(true);
}

/**
 * If there screen is off or in clock is displayed,
 * this will return true. This allows us to throw away
 * the first input from the rotary encoder but
 * to wake up the screen.
 */
bool DMXDisplay::wakeDisplay() {
  if (type == NONE || !enabled) return false;
  if (displayTurnedOff) {
  #if defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS)
    unsigned long now = millis();
    while (drawing && millis()-now < 125) delay(1); // wait if someone else is drawing
    if (drawing || lockRedraw) return false;
  #endif
    lockRedraw = true;
    clear();
    // Turn the display back on
    sleepDisplay(false);
    lockRedraw = false;
    return true;
  }
  return false;
}

#ifndef ARDUINO_RUNNING_CORE
  #if CONFIG_FREERTOS_UNICORE
    #define ARDUINO_RUNNING_CORE 0
  #else
    #define ARDUINO_RUNNING_CORE 1
  #endif
#endif
void DMXDisplay::onUpdateBegin(bool init) {
#if defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS)
  if (init && Display_Task) {
    vTaskSuspend(Display_Task);   // update is about to begin, disable task to prevent crash
  } else {
    // update has failed or create task requested
    if (Display_Task)
      vTaskResume(Display_Task);
    else
      xTaskCreatePinnedToCore(
        [](void * par) {                  // Function to implement the task
          // see https://www.freertos.org/vtaskdelayuntil.html
          const TickType_t xFrequency = REFRESH_RATE_MS * portTICK_PERIOD_MS / 2;
          TickType_t xLastWakeTime = xTaskGetTickCount();
          for(;;) {
            delay(1); // DO NOT DELETE THIS LINE! It is needed to give the IDLE(0) task enough time and to keep the watchdog happy.
                      // taskYIELD(), yield(), vTaskDelay() and esp_task_wdt_feed() didn't seem to work.
            vTaskDelayUntil(&xLastWakeTime, xFrequency); // release CPU, by doing nothing for REFRESH_RATE_MS millis
            DMXDisplay::getInstance()->redraw(false);
          }
        },
        "4LD",                // Name of the task
        3072,                 // Stack size in words
        NULL,                 // Task input parameter
        1,                    // Priority of the task (not idle)
        &Display_Task,        // Task handle
        ARDUINO_RUNNING_CORE
      );
  }
#endif
}

void DMXDisplay::appendConfigData() {
  oappend(F("dd=addDropdown('DMXDisplay','type');"));
  oappend(F("addOption(dd,'None',0);"));
  oappend(F("addOption(dd,'SSD1306',1);"));
  oappend(F("addOption(dd,'SH1106',2);"));
  oappend(F("addOption(dd,'SSD1306 128x64',3);"));
  oappend(F("addOption(dd,'SSD1305',4);"));
  oappend(F("addOption(dd,'SSD1305 128x64',5);"));
  oappend(F("addOption(dd,'SSD1309 128x64',9);"));
  oappend(F("addOption(dd,'SSD1306 SPI',6);"));
  oappend(F("addOption(dd,'SSD1306 SPI 128x64',7);"));
  oappend(F("addOption(dd,'SSD1309 SPI 128x64',8);"));
  oappend(F("addInfo('DMXDisplay:type',1,'<br><i class=\"warn\">Change may require reboot</i>','');"));
  oappend(F("addInfo('DMXDisplay:button_pin',0,'','Button Pin');"));
  oappend(F("addInfo('DMXDisplay:pin[]',0,'','SPI CS');"));
  oappend(F("addInfo('DMXDisplay:pin[]',1,'','SPI DC');"));
  oappend(F("addInfo('DMXDisplay:pin[]',2,'','SPI RST');"));
}

/*
  * addToConfig() can be used to add custom persistent settings to the cfg.json file in the "um" (usermod) object.
  * It will be called by WLED when settings are actually saved (for example, LED settings are saved)
  * If you want to force saving the current state, use serializeConfig() in your loop().
  *
  * CAUTION: serializeConfig() will initiate a filesystem write operation.
  * It might cause the LEDs to stutter and will cause flash wear if called too often.
  * Use it sparingly and always in the loop, never in network callbacks!
  *
  * addToConfig() will also not yet add your setting to one of the settings pages automatically.
  * To make that work you still have to add the setting to the HTML, xml.cpp and set.cpp manually.
  *
  * I highly recommend checking out the basics of ArduinoJson serialization and deserialization in order to use custom settings!
  */
void DMXDisplay::addToConfig(JsonObject& root) {
  JsonObject top   = root.createNestedObject(FPSTR(_name));
  top[FPSTR(_enabled)]       = enabled;

  top["type"]                = type;
  JsonArray io_pin = top.createNestedArray("pin");
  for (int i=0; i<3; i++) io_pin.add(ioPin[i]);
  top[FPSTR(_flip)]          = (bool) flip;
  top[FPSTR(_contrast)]      = contrast;
  top[FPSTR(_contrastFix)]   = (bool) contrastFix;
  #ifndef ARDUINO_ARCH_ESP32
  top[FPSTR(_refreshRate)]   = refreshRate;
  #endif
  top[FPSTR(_screenTimeOut)] = screenTimeout/1000;
  top[FPSTR(_sleepMode)]     = (bool) sleepMode;
  top[FPSTR(_busClkFrequency)] = ioFrequency/1000;

  top["button_pin"] = buttonPin;

  DEBUG_PRINTLN(F("DMX Display config saved."));

}

/*
  * readFromConfig() can be used to read back the custom settings you added with addToConfig().
  * This is called by WLED when settings are loaded (currently this only happens once immediately after boot)
  *
  * readFromConfig() is called BEFORE setup(). This means you can use your persistent values in setup() (e.g. pin assignments, buffer sizes),
  * but also that if you want to write persistent values to a dynamic buffer, you'd need to allocate it here instead of in setup.
  * If you don't know what that is, don't fret. It most likely doesn't affect your use case :)
  */
bool DMXDisplay::readFromConfig(JsonObject& root) {
  bool needsRedraw    = false;
  DisplayType newType = type;
  int8_t oldPin[3]; for (unsigned i=0; i<3; i++) oldPin[i] = ioPin[i];

  JsonObject top = root[FPSTR(_name)];
  if (top.isNull()) {
    DEBUG_PRINT(FPSTR(_name));
    DEBUG_PRINTLN(F(": No config found. (Using defaults.)"));
    return false;
  }

  enabled       = top[FPSTR(_enabled)] | enabled;
  newType       = top["type"] | newType;
  for (unsigned i=0; i<3; i++) ioPin[i] = top["pin"][i] | ioPin[i];
  flip          = top[FPSTR(_flip)] | flip;
  contrast      = top[FPSTR(_contrast)] | contrast;
  #ifndef ARDUINO_ARCH_ESP32
  refreshRate   = top[FPSTR(_refreshRate)] | refreshRate;
  refreshRate   = min(5000, max(250, (int)refreshRate));
  #endif
  screenTimeout = (top[FPSTR(_screenTimeOut)] | screenTimeout/1000) * 1000;
  sleepMode     = top[FPSTR(_sleepMode)] | sleepMode;
  contrastFix   = top[FPSTR(_contrastFix)] | contrastFix;
  buttonPin     = top["button_pin"] | -1;
  
  if (newType == SSD1306_SPI || newType == SSD1306_SPI64)
    ioFrequency = min(20000, max(500, (int)(top[FPSTR(_busClkFrequency)] | ioFrequency/1000))) * 1000;  // limit frequency
  else
    ioFrequency = min(3400, max(100, (int)(top[FPSTR(_busClkFrequency)] | ioFrequency/1000))) * 1000;  // limit frequency
    

  DEBUG_PRINT(FPSTR(_name));
  if (!initDone) {
    // first run: reading from cfg.json
    type = newType;
    DEBUG_PRINTLN(F(" config loaded."));
  } else {
    DEBUG_PRINTLN(F(" config (re)loaded."));
    // changing parameters from settings page
    bool pinsChanged = false;
    for (unsigned i=0; i<3; i++) if (ioPin[i] != oldPin[i]) { pinsChanged = true; break; }
    if (pinsChanged || type!=newType) {
      bool isSPI = (type == SSD1306_SPI || type == SSD1306_SPI64 || type == SSD1309_SPI64);
      bool newSPI = (newType == SSD1306_SPI || newType == SSD1306_SPI64 || newType == SSD1309_SPI64);
      if (isSPI) {
        if (pinsChanged || !newSPI) PinManager::deallocateMultiplePins((const uint8_t*)oldPin, 3, PinOwner::UM_DMXDisplay);
        if (!newSPI) {
          // was SPI but is no longer SPI
          if (i2c_scl<0 || i2c_sda<0) { newType=NONE; }
        } else {
          // still SPI but pins changed
          PinManagerPinType cspins[3] = { { ioPin[0], true }, { ioPin[1], true }, { ioPin[2], true } };
          if (ioPin[0]<0 || ioPin[1]<0 || ioPin[1]<0) { newType=NONE; }
          else if (!PinManager::allocateMultiplePins(cspins, 3, PinOwner::UM_DMXDisplay)) { newType=NONE; }
        }
      } else if (newSPI) {
        // was I2C but is now SPI
        if (spi_sclk<0 || spi_mosi<0) {
          newType=NONE;
        } else {
          PinManagerPinType pins[3] = { { ioPin[0], true }, { ioPin[1], true }, { ioPin[2], true } };
          if (ioPin[0]<0 || ioPin[1]<0 || ioPin[1]<0) { newType=NONE; }
          else if (!PinManager::allocateMultiplePins(pins, 3, PinOwner::UM_DMXDisplay)) { newType=NONE; }
        }
      } else {
        // just I2C type changed
      }
      type = newType;
      switch (type) {
        case SSD1306:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1306_128x32_univision, u8x8_cad_ssd13xx_fast_i2c, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_HW_I2C(u8x8->getU8x8(), U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
          break;
        case SH1106:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_sh1106_128x64_winstar, u8x8_cad_ssd13xx_fast_i2c, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_HW_I2C(u8x8->getU8x8(), U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
          break;
        case SSD1306_64:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1306_128x64_noname, u8x8_cad_ssd13xx_fast_i2c, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_HW_I2C(u8x8->getU8x8(), U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
          break;
        case SSD1305:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1305_128x32_adafruit, u8x8_cad_ssd13xx_fast_i2c, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_HW_I2C(u8x8->getU8x8(), U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
          break;
        case SSD1305_64:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1305_128x64_adafruit, u8x8_cad_ssd13xx_fast_i2c, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_HW_I2C(u8x8->getU8x8(), U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
          break;
        case SSD1309_64:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1309_128x64_noname0, u8x8_cad_ssd13xx_fast_i2c, u8x8_byte_arduino_hw_i2c, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_HW_I2C(u8x8->getU8x8(), U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
          break;
        case SSD1306_SPI:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1306_128x32_univision, u8x8_cad_001, u8x8_byte_arduino_hw_spi, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_4Wire_HW_SPI(u8x8->getU8x8(), ioPin[0], ioPin[1], ioPin[2]); // Pins are cs, dc, reset
          break;
        case SSD1306_SPI64:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1306_128x64_noname, u8x8_cad_001, u8x8_byte_arduino_hw_spi, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_4Wire_HW_SPI(u8x8->getU8x8(), ioPin[0], ioPin[1], ioPin[2]); // Pins are cs, dc, reset
          break;
        case SSD1309_SPI64:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_ssd1309_128x64_noname0, u8x8_cad_001, u8x8_byte_arduino_hw_spi, u8x8_gpio_and_delay_arduino);
          u8x8_SetPin_4Wire_HW_SPI(u8x8->getU8x8(), ioPin[0], ioPin[1], ioPin[2]); // Pins are cs, dc, reset
        default:
          u8x8_Setup(u8x8->getU8x8(), u8x8_d_null_cb, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
          enabled = false;
          break;
      }
      startDisplay();
      needsRedraw |= true;
    } else {
      u8x8->setBusClock(ioFrequency); // can be used for SPI too
      setVcomh(contrastFix);
      setContrast(contrast);
      setFlipMode(flip);
    }
  }
  return !top[FPSTR(_contrastFix)].isNull();
}


// static DMXDisplay usermod_v2_dmx_display;
// REGISTER_USERMOD(usermod_v2_dmx_display);
