#include "wled.h"
#undef U8X8_NO_HW_I2C // borrowed from WLEDMM: we do want I2C hardware drivers - if possible
#include <U8x8lib.h> // from https://github.com/olikraus/u8g2/

#pragma once

#ifndef FLD_ESP32_NO_THREADS
  #define FLD_ESP32_USE_THREADS  // comment out to use 0.13.x behaviour without parallel update task - slower, but more robust. May delay other tasks like LEDs or audioreactive!!
#endif

#ifndef FLD_PIN_CS
  #define FLD_PIN_CS 5
#endif

#ifdef ARDUINO_ARCH_ESP32
  #ifndef FLD_PIN_DC
    #define FLD_PIN_DC 19
  #endif
  #ifndef FLD_PIN_RESET
    #define FLD_PIN_RESET 26
  #endif
#else
  #ifndef FLD_PIN_DC
    #define FLD_PIN_DC 12
  #endif
  #ifndef FLD_PIN_RESET
    #define FLD_PIN_RESET 16
  #endif
#endif

#ifndef FLD_TYPE
  #ifndef FLD_SPI_DEFAULT
    #define FLD_TYPE SSD1306
  #else
    #define FLD_TYPE SSD1306_SPI
  #endif
#endif

// When to time out to the clock or blank the screen
// if SLEEP_MODE_ENABLED.
#define SCREEN_TIMEOUT_MS  60*1000    // 1 min

// Minimum time between redrawing screen in ms
#define REFRESH_RATE_MS 1000

// Extra char (+1) for null
#define LINE_BUFFER_SIZE            16+1
#define MAX_JSON_CHARS              19+1
#define MAX_MODE_LINE_SPACE         13+1

typedef enum {
  NONE = 0,
  SSD1306,          // U8X8_SSD1306_128X32_UNIVISION_HW_I2C
  SH1106,           // U8X8_SH1106_128X64_WINSTAR_HW_I2C
  SSD1306_64,       // U8X8_SSD1306_128X64_NONAME_HW_I2C
  SSD1305,          // U8X8_SSD1305_128X32_ADAFRUIT_HW_I2C
  SSD1305_64,       // U8X8_SSD1305_128X64_ADAFRUIT_HW_I2C
  SSD1306_SPI,      // U8X8_SSD1306_128X32_NONAME_HW_SPI
  SSD1306_SPI64,    // U8X8_SSD1306_128X64_NONAME_HW_SPI
  SSD1309_SPI64,    // U8X8_SSD1309_128X64_NONAME0_4W_HW_SPI
  SSD1309_64        // U8X8_SSD1309_128X64_NONAME0_HW_I2C
} DisplayType;

class DMXDisplay : public Usermod {
  #if defined(ARDUINO_ARCH_ESP32) && defined(FLD_ESP32_USE_THREADS)
    public:
      DMXDisplay() { if (!instance) instance = this; }
      static DMXDisplay* getInstance(void) { return instance; }
  #endif
  
    private:
  
      static DMXDisplay *instance;
      bool initDone = false;
      volatile bool drawing = false;
      volatile bool lockRedraw = false;
  
      // HW interface & configuration
      U8X8 *u8x8 = nullptr;           // pointer to U8X8 display object
  
      #ifndef FLD_SPI_DEFAULT
      int8_t ioPin[3] = {-1, -1, -1}; // I2C pins: SCL, SDA
      uint32_t ioFrequency = 400000;  // in Hz (minimum is 100000, baseline is 400000 and maximum should be 3400000)
      #else
      int8_t ioPin[3] = {FLD_PIN_CS, FLD_PIN_DC, FLD_PIN_RESET}; // custom SPI pins: CS, DC, RST
      uint32_t ioFrequency = 1000000;  // in Hz (minimum is 500kHz, baseline is 1MHz and maximum should be 20MHz)
      #endif

      int8_t buttonPin = -1;
  
      DisplayType type = FLD_TYPE;    // display type
      bool flip = false;              // flip display 180°
      uint8_t contrast = 10;          // screen contrast
      uint8_t lineHeight = 1;         // 1 row or 2 rows
      uint16_t refreshRate = REFRESH_RATE_MS;     // in ms
      uint32_t screenTimeout = SCREEN_TIMEOUT_MS; // in ms
      bool sleepMode = true;          // allow screen sleep?
      // bool clockMode = false;         // display clock
      // bool showSeconds = true;        // display clock with seconds
      bool enabled = true;
      bool contrastFix = false;
  
      // Next variables hold the previous known values to determine if redraw is
      // required.
      String knownSsid = apSSID;
      String knownPassword = apPass;
      IPAddress knownIp = IPAddress(4, 3, 2, 1);
      uint8_t knownDMXAddress = 0;
      uint8_t knownDMXUniverse = 0;
      bool wificonnected = interfacesInited;
      bool powerON = true;

      bool displayTurnedOff = false;
      unsigned long nextUpdate = 0;
      unsigned long lastRedraw = 0;
  
      // strings to reduce flash memory usage (used more than twice)
      static const char _name[];
      static const char _enabled[];
      static const char _contrast[];
      static const char _refreshRate[];
      static const char _screenTimeOut[];
      static const char _flip[];
      static const char _sleepMode[];
      static const char _busClkFrequency[];
      static const char _contrastFix[];

      enum DisplayMode {
        MODE_DMX = 0,
        MODE_NET = 1,
        MODE_OFF = 2
      };

      DisplayMode currentMode = MODE_NET;

      // Button handling
      unsigned long buttonDownTime = 0;
      bool buttonHeld = false;
      bool buttonWasPressed = false;

      bool btnLastState = true;    // pull-up based, so HIGH = unpressed
      unsigned long btnPressStart = 0;


  
      // If display does not work or looks corrupted check the
      // constructor reference:
      // https://github.com/olikraus/u8g2/wiki/u8x8setupcpp
      // or check the gallery:
      // https://github.com/olikraus/u8g2/wiki/gallery
  
      // some displays need this to properly apply contrast
      void setVcomh(bool highContrast);
      void startDisplay();
  
      /**
       * Wrappers for screen drawing
       */
      void setFlipMode(uint8_t mode);
      void setContrast(uint8_t contrast);
      void drawString(uint8_t col, uint8_t row, const char *string, bool ignoreLH=false);
      void draw2x2String(uint8_t col, uint8_t row, const char *string);
      uint8_t getCols();
      void clear();
      void setPowerSave(uint8_t save);
      void center(String &line, uint8_t width);
  
      /**
       * Enable sleep (turn the display off) or clock mode.
       */
      void sleepDisplay(bool enabled);
  
    public:
  
      // gets called once at boot. Do all initialization that doesn't depend on
      // network here
      void setup() override;
  
      // gets called every time WiFi is (re-)connected. Initialize own network
      // interfaces here
      void connected() override;
  
      /**
       * Da loop.
       */
      void loop() override;
  
      //function to update lastredraw
      inline void updateRedrawTime() { lastRedraw = millis(); }
  
      /**
       * Redraw the screen (but only if things have changed
       * or if forceRedraw).
       */
      void redraw(bool forceRedraw);
  
      void updateDMX();
      void updateNetworkInfo();
  
  
      /**
       * If there screen is off or in clock is displayed,
       * this will return true. This allows us to throw away
       * the first input from the rotary encoder but
       * to wake up the screen.
       */
      bool wakeDisplay();
  
      // /**
      //  * Allows you to show one line and a glyph as overlay for a period of time.
      //  * Clears the screen and prints.
      //  * Used in Rotary Encoder usermod.
      //  */
      // void overlay(const char* line1, long showHowLong, byte glyphType);
  
      // /**
      //  * Allows you to show Akemi WLED logo overlay for a period of time.
      //  * Clears the screen and prints.
      //  */
      // void overlayLogo(long showHowLong);
  
      // /**
      //  * Allows you to show two lines as overlay for a period of time.
      //  * Clears the screen and prints.
      //  * Used in Auto Save usermod
      //  */
      // void overlay(const char* line1, const char* line2, long showHowLong);
  
      // void networkOverlay(const char* line1, long showHowLong);
  
      /**
       * handleButton() can be used to override default button behaviour. Returning true
       * will prevent button working in a default way.
       * Replicating button.cpp
       */
      // bool handleButton(uint8_t b);

      void setMode(DisplayMode m);

      // void renderSSIDToBuffer(const String& ssid);
  
      void onUpdateBegin(bool init) override;
  
      /*
       * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
       * Creating an "u" object allows you to add custom key/value pairs to the Info section of the WLED web UI.
       * Below it is shown how this could be used for e.g. a light sensor
       */
      //void addToJsonInfo(JsonObject& root) override;
  
      /*
       * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
       * Values in the state object may be modified by connected clients
       */
      //void addToJsonState(JsonObject& root) override;
  
      /*
       * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API (state object).
       * Values in the state object may be modified by connected clients
       */
      //void readFromJsonState(JsonObject& root) override;
  
      void appendConfigData() override;
  
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
      void addToConfig(JsonObject& root) override;
  
      /*
       * readFromConfig() can be used to read back the custom settings you added with addToConfig().
       * This is called by WLED when settings are loaded (currently this only happens once immediately after boot)
       *
       * readFromConfig() is called BEFORE setup(). This means you can use your persistent values in setup() (e.g. pin assignments, buffer sizes),
       * but also that if you want to write persistent values to a dynamic buffer, you'd need to allocate it here instead of in setup.
       * If you don't know what that is, don't fret. It most likely doesn't affect your use case :)
       */
      bool readFromConfig(JsonObject& root) override;
  
      /*
       * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
       * This could be used in the future for the system to determine whether your usermod is installed.
       */
      uint16_t getId() override {
        return USERMOD_ID_DMX_DISPLAY;
      }
  };
  