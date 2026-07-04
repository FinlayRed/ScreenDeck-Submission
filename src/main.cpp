#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <esp_io_expander.hpp>
#include <USB.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>
#if __has_include(<USBCDC.h>)
#include <USBCDC.h>
#define HAS_USB_CDC_CLASS 1
#else
#define HAS_USB_CDC_CLASS 0
#endif
#include <ArduinoJson.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "esp_jpeg_dec.h"
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cstdarg>
#include <cmath>
#include "lvgl_v8_port.h"
#include "waveshare_sd_card.h"

// Extend IO Pin define (CH422G channel indices)
#define LCD_BL 2
#define SD_CS 4
#define USB_SEL 5
// Grid configuration
#define GRID_ROWS 4
#define GRID_COLS 8
#define BUTTON_SIZE 85  // Size of each button
#define BUTTON_GAP 17   // Gap between buttons
#define TOTAL_BUTTONS (GRID_ROWS * GRID_COLS)
#define BUTTON_TAP_ZOOM 272
#define BUTTON_TAP_PRESS_MS 70
#define BUTTON_TAP_RELEASE_MS 140

// Macro configuration
#define MACRO_CONFIG_PATH "/macros.json"
#define MACRO_CONFIG_VERSION 2
#define MAX_ACTIONS_PER_ICON 24
#define FALLBACK_ICON_PATH "/fallback.bin"
#define ICON_FORMAT "/icon_%d_%d.bin"  // Format: icon_row_col.bin
#define RADIAL_ICON_FORMAT "/radial_%d_%d_%s.bin"
#define RADIAL_DIRECTION_COUNT 4
#define RADIAL_MENU_GAP 17
#define RADIAL_MENU_OPEN_DRAG_PX 5
#define RADIAL_MENU_DEADZONE_PX 20
#define SCREENSAVER_DIR_PATH "/screensavers"
#define SCREENSAVER_ACTIVE_PATH "/screensavers/active.sdmj"
#define SCREENSAVER_ACTIVE_SDRA_PATH "/screensavers/active.sdra"
#define CDC_MAX_TRANSFER_BYTES (64UL * 1024UL * 1024UL)
#define CDC_UPLOAD_BUFFER_BYTES 1024
#define SD_SPI_FREQUENCY_HZ 40000000UL
#define SDMJ_HEADER_SIZE 40
#define SDMJ_INDEX_ENTRY_SIZE 8
#define SDMJ_VERSION 1
#define SDMJ_WIDTH 800
#define SDMJ_HEIGHT 480
#define SDMJ_MAX_FRAME_COUNT 10000
#define SDRA_HEADER_SIZE 40
#define SDRA_INDEX_ENTRY_SIZE 8
#define SDRA_FRAME_HEADER_SIZE 4
#define SDRA_VERSION 1
#define SDRA_FRAME_FLAG_FULL 1

#ifndef KEY_LEFT_CTRL
#define KEY_LEFT_CTRL 0x80
#endif
#ifndef KEY_LEFT_SHIFT
#define KEY_LEFT_SHIFT 0x81
#endif
#ifndef KEY_LEFT_ALT
#define KEY_LEFT_ALT 0x82
#endif
#ifndef KEY_LEFT_GUI
#define KEY_LEFT_GUI 0x83
#endif
#ifndef KEY_RETURN
#define KEY_RETURN 0xB0
#endif
#ifndef KEY_ESC
#define KEY_ESC 0xB1
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 0xB2
#endif
#ifndef KEY_TAB
#define KEY_TAB 0xB3
#endif
#ifndef KEY_CAPS_LOCK
#define KEY_CAPS_LOCK 0xC1
#endif
#ifndef KEY_F1
#define KEY_F1 0xC2
#endif
#ifndef KEY_F2
#define KEY_F2 0xC3
#endif
#ifndef KEY_F3
#define KEY_F3 0xC4
#endif
#ifndef KEY_F4
#define KEY_F4 0xC5
#endif
#ifndef KEY_F5
#define KEY_F5 0xC6
#endif
#ifndef KEY_F6
#define KEY_F6 0xC7
#endif
#ifndef KEY_F7
#define KEY_F7 0xC8
#endif
#ifndef KEY_F8
#define KEY_F8 0xC9
#endif
#ifndef KEY_F9
#define KEY_F9 0xCA
#endif
#ifndef KEY_F10
#define KEY_F10 0xCB
#endif
#ifndef KEY_F11
#define KEY_F11 0xCC
#endif
#ifndef KEY_F12
#define KEY_F12 0xCD
#endif
#ifndef KEY_INSERT
#define KEY_INSERT 0xD1
#endif
#ifndef KEY_HOME
#define KEY_HOME 0xD2
#endif
#ifndef KEY_PAGE_UP
#define KEY_PAGE_UP 0xD3
#endif
#ifndef KEY_DELETE
#define KEY_DELETE 0xD4
#endif
#ifndef KEY_END
#define KEY_END 0xD5
#endif
#ifndef KEY_PAGE_DOWN
#define KEY_PAGE_DOWN 0xD6
#endif
#ifndef KEY_RIGHT_ARROW
#define KEY_RIGHT_ARROW 0xD7
#endif
#ifndef KEY_LEFT_ARROW
#define KEY_LEFT_ARROW 0xD8
#endif
#ifndef KEY_DOWN_ARROW
#define KEY_DOWN_ARROW 0xD9
#endif
#ifndef KEY_UP_ARROW
#define KEY_UP_ARROW 0xDA
#endif

#ifndef KEY_F13
#define KEY_F13 0xF0
#endif
#ifndef KEY_F14
#define KEY_F14 0xF1
#endif
#ifndef KEY_F15
#define KEY_F15 0xF2
#endif
#ifndef KEY_F16
#define KEY_F16 0xF3
#endif
#ifndef KEY_F17
#define KEY_F17 0xF4
#endif
#ifndef KEY_F18
#define KEY_F18 0xF5
#endif
#ifndef KEY_F19
#define KEY_F19 0xF6
#endif
#ifndef KEY_F20
#define KEY_F20 0xF7
#endif
#ifndef KEY_F21
#define KEY_F21 0xF8
#endif
#ifndef KEY_F22
#define KEY_F22 0xF9
#endif
#ifndef KEY_F23
#define KEY_F23 0xFA
#endif
#ifndef KEY_F24
#define KEY_F24 0xFB
#endif

// Screensaver configuration
#define SCREENSAVER_TIMEOUT_MS 600000      // 1 minute
#define SCREENSAVER_CHECK_INTERVAL_MS 1000

// Forward declarations (used before definitions)
void activateScreensaver();
void deactivateScreensaver();
void rebuildGridUI();
void processCdcInput();
static void btn_event_handler(lv_event_t* e);
extern unsigned long lastActivityTime;

// Global variables
bool sdCardInitialized = false;
esp_expander::Base* expander =
    nullptr;  // Global expander instance for backlight control
USBHIDKeyboard keyboard;
bool usbKeyboardReady = false;
bool usbInitAttempted = false;
#if HAS_USB_CDC_CLASS
USBCDC cdcPort;
bool usbCdcReady = false;
#endif

enum class MacroActionType : uint8_t {
  Combo = 0,
  Delay = 1,
};

enum MacroModifierBits : uint8_t {
  MacroModCtrl = 1 << 0,
  MacroModShift = 1 << 1,
  MacroModAlt = 1 << 2,
  MacroModGui = 1 << 3,
};

struct MacroAction {
  MacroActionType type = MacroActionType::Delay;
  uint8_t modifiers = 0;
  uint8_t keycode = 0;
  uint16_t delayMs = 0;
};

struct MacroSequence {
  MacroAction actions[MAX_ACTIONS_PER_ICON];
  uint8_t actionCount = 0;
};

struct RadialMacroItem : MacroSequence {
  bool configured = false;
};

struct IconMacro : MacroSequence {
  bool radialEnabled = false;
  RadialMacroItem radialItems[RADIAL_DIRECTION_COUNT];
};

struct MacroExecutorState {
  bool active = false;
  bool comboPressed = false;
  uint8_t actionIndex = 0;
  uint8_t actionCount = 0;
  const MacroAction* actions = nullptr;
  unsigned long nextActionAtMs = 0;
};

struct CdcUploadSession {
  bool active = false;
  bool chunked = false;
  char targetPath[64] = {0};
  char tempPath[72] = {0};
  size_t expectedBytes = 0;
  size_t receivedBytes = 0;
  uint8_t chunkHeader[2] = {0};
  uint8_t chunkHeaderBytes = 0;
  uint16_t currentChunkSize = 0;
  uint16_t currentChunkBytes = 0;
  unsigned long lastDataMs = 0;
  uint8_t sourcePort = 0;
  File file;
};

struct SdmjFileStatus {
  bool ok = false;
  char error[32] = {0};
  uint32_t fileSize = 0;
  uint32_t frameCount = 0;
  uint32_t indexOffset = 0;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;
  uint32_t maxFrameSize = 0;
  uint32_t crc32 = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t fps = 0;
};

struct SdraFileStatus {
  bool ok = false;
  char error[32] = {0};
  uint32_t fileSize = 0;
  uint32_t frameCount = 0;
  uint32_t indexOffset = 0;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;
  uint32_t maxFrameSize = 0;
  uint32_t crc32 = 0;
  uint32_t fullFrameBytes = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t fps = 0;
  uint16_t tileSize = 0;
  uint16_t tilesX = 0;
  uint16_t tilesY = 0;
};

struct SdmjFrameEntry {
  uint32_t offset = 0;
  uint32_t size = 0;
};

struct SdmjPlaybackStats {
  uint32_t framesPresented = 0;
  uint32_t loopsCompleted = 0;
  uint32_t droppedFrames = 0;
  uint32_t decodeErrors = 0;
  uint32_t maxFrameWorkUs = 0;
  uint64_t totalReadUs = 0;
  uint64_t totalDecodeUs = 0;
  uint64_t totalSwitchUs = 0;
  uint64_t totalPlayUs = 0;
};

enum class CdcPortId : uint8_t {
  SerialPort = 0,
  UsbCdcPort = 1,
};

IconMacro g_iconMacros[TOTAL_BUTTONS];
MacroExecutorState g_macroExecutor;
CdcUploadSession g_cdcUpload;

struct RadialMenuState {
  bool active = false;
  bool suppressNextClick = false;
  uint8_t iconIndex = 0;
  int row = 0;
  int col = 0;
  int selectedDirection = -1;
  lv_point_t origin = {0, 0};
  lv_point_t pressPoint = {0, 0};
  lv_point_t bestPoint = {0, 0};
  int bestDistance = 0;
  bool hasPressPoint = false;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* sourceButton = nullptr;
  lv_obj_t* itemObjects[RADIAL_DIRECTION_COUNT] = {nullptr};
};

RadialMenuState g_radialMenu;

static lv_style_t g_buttonTapStyleDefault;
static lv_style_t g_buttonTapStylePressed;
static bool g_buttonTapStylesInitialized = false;

char g_serialLineBuffer[160] = {0};
size_t g_serialLineLength = 0;
#if HAS_USB_CDC_CLASS
char g_usbLineBuffer[160] = {0};
size_t g_usbLineLength = 0;
#endif

constexpr uint16_t kComboHoldMs = 12;
constexpr uint16_t kComboGapMs = 8;
constexpr unsigned long kCdcUploadTimeoutMs = 180000;

esp_panel::drivers::LCD* g_lcd = nullptr;
esp_panel::drivers::Touch* g_touch = nullptr;

static void ensureButtonTapStyles() {
  if (g_buttonTapStylesInitialized) {
    return;
  }

  static const lv_style_prop_t transitionProps[] = {LV_STYLE_TRANSFORM_ZOOM,
                                                    LV_STYLE_PROP_INV};
  static lv_style_transition_dsc_t releaseTransition;
  static lv_style_transition_dsc_t pressTransition;

  lv_style_transition_dsc_init(&releaseTransition, transitionProps,
                               lv_anim_path_overshoot,
                               BUTTON_TAP_RELEASE_MS, 0, nullptr);
  lv_style_transition_dsc_init(&pressTransition, transitionProps,
                               lv_anim_path_ease_out,
                               BUTTON_TAP_PRESS_MS, 0, nullptr);

  lv_style_init(&g_buttonTapStyleDefault);
  lv_style_set_transform_pivot_x(&g_buttonTapStyleDefault, BUTTON_SIZE / 2);
  lv_style_set_transform_pivot_y(&g_buttonTapStyleDefault, BUTTON_SIZE / 2);
  lv_style_set_transition(&g_buttonTapStyleDefault, &releaseTransition);

  lv_style_init(&g_buttonTapStylePressed);
  lv_style_set_transform_zoom(&g_buttonTapStylePressed, BUTTON_TAP_ZOOM);
  lv_style_set_transition(&g_buttonTapStylePressed, &pressTransition);

  g_buttonTapStylesInitialized = true;
}

static Stream* streamForPortId(CdcPortId portId) {
  if (portId == CdcPortId::SerialPort) {
    return &Serial;
  }

#if HAS_USB_CDC_CLASS
  if (usbCdcReady) {
    return &cdcPort;
  }
#endif

  return &Serial;
}

static void cdcBroadcast(const char* fmt, ...) {
  char message[320];

  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  Serial.println(message);

#if HAS_USB_CDC_CLASS
  if (usbCdcReady) {
    cdcPort.println(message);
  }
#endif
}

static void cdcReplyToPort(CdcPortId portId, const char* fmt, ...) {
  char message[320];

  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  Stream* cdcStream = streamForPortId(portId);
  cdcStream->println(message);
  if (cdcStream != &Serial) {
    Serial.println(message);
  }
}

static void emitButtonEvent(uint8_t iconIndex, int row, int col) {
  if (g_cdcUpload.active) {
    return;
  }

  cdcBroadcast("CDC:EVENT BUTTON %u %d %d", iconIndex, row, col);
}

static void emitRadialEvent(uint8_t iconIndex, int row, int col, const char* direction) {
  if (g_cdcUpload.active || direction == nullptr) {
    return;
  }

  cdcBroadcast("CDC:EVENT RADIAL %u %d %d %s", iconIndex, row, col, direction);
}

[[noreturn]] static void haltWithError(const char* message) {
  Serial.println(message);
  while (true) {
    delay(1000);
  }
}

static bool equalsIgnoreCase(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }

  while (*lhs != '\0' && *rhs != '\0') {
    if (std::toupper(static_cast<unsigned char>(*lhs)) !=
        std::toupper(static_cast<unsigned char>(*rhs))) {
      return false;
    }
    lhs++;
    rhs++;
  }

  return *lhs == '\0' && *rhs == '\0';
}

static bool startsWithIgnoreCase(const char* value, const char* prefix) {
  if (value == nullptr || prefix == nullptr) {
    return false;
  }

  while (*prefix != '\0') {
    if (*value == '\0') {
      return false;
    }
    if (std::toupper(static_cast<unsigned char>(*value)) !=
        std::toupper(static_cast<unsigned char>(*prefix))) {
      return false;
    }
    value++;
    prefix++;
  }

  return true;
}

static const char* radialDirectionName(uint8_t directionIndex) {
  static const char* kDirectionNames[RADIAL_DIRECTION_COUNT] = {
      "n", "e", "s", "w"};

  if (directionIndex >= RADIAL_DIRECTION_COUNT) {
    return "";
  }
  return kDirectionNames[directionIndex];
}

static bool radialDirectionIndexFromName(const char* name, uint8_t& directionIndex) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  for (uint8_t i = 0; i < RADIAL_DIRECTION_COUNT; i++) {
    if (equalsIgnoreCase(name, radialDirectionName(i))) {
      directionIndex = i;
      return true;
    }
  }

  return false;
}

static bool radialDirectionOffset(uint8_t directionIndex, int& offsetX, int& offsetY) {
  static const int8_t kOffsets[RADIAL_DIRECTION_COUNT][2] = {
      {0, -1}, {1, 0}, {0, 1}, {-1, 0}};

  if (directionIndex >= RADIAL_DIRECTION_COUNT) {
    return false;
  }

  offsetX = kOffsets[directionIndex][0];
  offsetY = kOffsets[directionIndex][1];
  return true;
}

static bool parseFunctionKeyName(const char* keyName, uint8_t& keycode) {
  if (keyName == nullptr || std::toupper(static_cast<unsigned char>(keyName[0])) != 'F') {
    return false;
  }

  char* endPtr = nullptr;
  long functionIndex = strtol(keyName + 1, &endPtr, 10);
  if (endPtr == nullptr || *endPtr != '\0') {
    return false;
  }

  switch (functionIndex) {
    case 1: keycode = KEY_F1; return true;
    case 2: keycode = KEY_F2; return true;
    case 3: keycode = KEY_F3; return true;
    case 4: keycode = KEY_F4; return true;
    case 5: keycode = KEY_F5; return true;
    case 6: keycode = KEY_F6; return true;
    case 7: keycode = KEY_F7; return true;
    case 8: keycode = KEY_F8; return true;
    case 9: keycode = KEY_F9; return true;
    case 10: keycode = KEY_F10; return true;
    case 11: keycode = KEY_F11; return true;
    case 12: keycode = KEY_F12; return true;
    case 13: keycode = KEY_F13; return true;
    case 14: keycode = KEY_F14; return true;
    case 15: keycode = KEY_F15; return true;
    case 16: keycode = KEY_F16; return true;
    case 17: keycode = KEY_F17; return true;
    case 18: keycode = KEY_F18; return true;
    case 19: keycode = KEY_F19; return true;
    case 20: keycode = KEY_F20; return true;
    case 21: keycode = KEY_F21; return true;
    case 22: keycode = KEY_F22; return true;
    case 23: keycode = KEY_F23; return true;
    case 24: keycode = KEY_F24; return true;
    default: return false;
  }
}

static bool keyNameToKeycode(const char* keyName, uint8_t& keycode) {
  if (keyName == nullptr || keyName[0] == '\0') {
    return false;
  }

  if (strlen(keyName) == 1) {
    char key = keyName[0];
    if (key >= 'A' && key <= 'Z') {
      key = static_cast<char>(key - 'A' + 'a');
    }
    keycode = static_cast<uint8_t>(key);
    return true;
  }

  if (parseFunctionKeyName(keyName, keycode)) {
    return true;
  }

  if (equalsIgnoreCase(keyName, "ENTER") || equalsIgnoreCase(keyName, "RETURN")) {
    keycode = KEY_RETURN;
    return true;
  }
  if (equalsIgnoreCase(keyName, "ESC") || equalsIgnoreCase(keyName, "ESCAPE")) {
    keycode = KEY_ESC;
    return true;
  }
  if (equalsIgnoreCase(keyName, "TAB")) {
    keycode = KEY_TAB;
    return true;
  }
  if (equalsIgnoreCase(keyName, "SPACE") || equalsIgnoreCase(keyName, "SPACEBAR")) {
    keycode = ' ';
    return true;
  }
  if (equalsIgnoreCase(keyName, "BACKSPACE")) {
    keycode = KEY_BACKSPACE;
    return true;
  }
  if (equalsIgnoreCase(keyName, "DELETE") || equalsIgnoreCase(keyName, "DEL")) {
    keycode = KEY_DELETE;
    return true;
  }
  if (equalsIgnoreCase(keyName, "INSERT") || equalsIgnoreCase(keyName, "INS")) {
    keycode = KEY_INSERT;
    return true;
  }
  if (equalsIgnoreCase(keyName, "HOME")) {
    keycode = KEY_HOME;
    return true;
  }
  if (equalsIgnoreCase(keyName, "END")) {
    keycode = KEY_END;
    return true;
  }
  if (equalsIgnoreCase(keyName, "PAGEUP") || equalsIgnoreCase(keyName, "PGUP")) {
    keycode = KEY_PAGE_UP;
    return true;
  }
  if (equalsIgnoreCase(keyName, "PAGEDOWN") || equalsIgnoreCase(keyName, "PGDN")) {
    keycode = KEY_PAGE_DOWN;
    return true;
  }
  if (equalsIgnoreCase(keyName, "UP")) {
    keycode = KEY_UP_ARROW;
    return true;
  }
  if (equalsIgnoreCase(keyName, "DOWN")) {
    keycode = KEY_DOWN_ARROW;
    return true;
  }
  if (equalsIgnoreCase(keyName, "LEFT")) {
    keycode = KEY_LEFT_ARROW;
    return true;
  }
  if (equalsIgnoreCase(keyName, "RIGHT")) {
    keycode = KEY_RIGHT_ARROW;
    return true;
  }
  if (equalsIgnoreCase(keyName, "CAPSLOCK")) {
    keycode = KEY_CAPS_LOCK;
    return true;
  }

  return false;
}

static bool parseIconPath(const char* path, int& row, int& col) {
  if (path == nullptr) {
    return false;
  }

  int parsedChars = 0;
  if (sscanf(path, "/icon_%d_%d.bin%n", &row, &col, &parsedChars) != 2) {
    return false;
  }

  if (path[parsedChars] != '\0') {
    return false;
  }

  return row >= 0 && row < GRID_ROWS && col >= 0 && col < GRID_COLS;
}

static bool parseRadialIconPath(const char* path, int& row, int& col, uint8_t& directionIndex) {
  if (path == nullptr) {
    return false;
  }

  char direction[4] = {0};
  int parsedChars = 0;
  if (sscanf(path, "/radial_%d_%d_%3[a-zA-Z]%n", &row, &col, direction,
             &parsedChars) != 3) {
    return false;
  }

  if (path[parsedChars] != '.' || strcmp(path + parsedChars, ".bin") != 0) {
    return false;
  }

  return row >= 0 && row < GRID_ROWS && col >= 0 && col < GRID_COLS &&
         radialDirectionIndexFromName(direction, directionIndex);
}

static bool isAllowedUploadPath(const char* path) {
  if (path == nullptr) {
    return false;
  }

  if (strcmp(path, MACRO_CONFIG_PATH) == 0 ||
      strcmp(path, FALLBACK_ICON_PATH) == 0 ||
      strcmp(path, SCREENSAVER_ACTIVE_PATH) == 0 ||
      strcmp(path, SCREENSAVER_ACTIVE_SDRA_PATH) == 0) {
    return true;
  }

  int row = -1;
  int col = -1;
  uint8_t directionIndex = 0;
  return parseIconPath(path, row, col) ||
         parseRadialIconPath(path, row, col, directionIndex);
}

static bool isIconAssetPath(const char* path) {
  if (path == nullptr) {
    return false;
  }

  if (strcmp(path, FALLBACK_ICON_PATH) == 0) {
    return true;
  }

  int row = -1;
  int col = -1;
  uint8_t directionIndex = 0;
  return parseIconPath(path, row, col) ||
         parseRadialIconPath(path, row, col, directionIndex);
}

static bool isScreensaverAssetPath(const char* path) {
  return path != nullptr &&
         (strcmp(path, SCREENSAVER_ACTIVE_PATH) == 0 ||
          strcmp(path, SCREENSAVER_ACTIVE_SDRA_PATH) == 0);
}

static void setSdmjError(SdmjFileStatus& status, const char* error) {
  snprintf(status.error, sizeof(status.error), "%s", error);
  status.ok = false;
}

static uint16_t readLe16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8);
}

static uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

static bool readExact(File& file, uint8_t* output, size_t byteCount) {
  return file.read(output, byteCount) == byteCount;
}

static bool validateActiveScreensaver(SdmjFileStatus& status) {
  memset(&status, 0, sizeof(status));

  if (!sdCardInitialized) {
    setSdmjError(status, "SD_NOT_READY");
    return false;
  }

  if (!SD.exists(SCREENSAVER_ACTIVE_PATH)) {
    setSdmjError(status, "NOT_FOUND");
    return false;
  }

  File file = SD.open(SCREENSAVER_ACTIVE_PATH, FILE_READ);
  if (!file) {
    setSdmjError(status, "OPEN_FAILED");
    return false;
  }

  status.fileSize = static_cast<uint32_t>(file.size());
  if (status.fileSize < SDMJ_HEADER_SIZE) {
    file.close();
    setSdmjError(status, "TOO_SMALL");
    return false;
  }

  uint8_t header[SDMJ_HEADER_SIZE] = {0};
  if (!readExact(file, header, sizeof(header))) {
    file.close();
    setSdmjError(status, "HEADER_READ");
    return false;
  }

  if (memcmp(header, "SDMJ", 4) != 0) {
    file.close();
    setSdmjError(status, "BAD_MAGIC");
    return false;
  }

  uint16_t version = readLe16(header + 4);
  uint16_t headerSize = readLe16(header + 6);
  status.width = readLe16(header + 8);
  status.height = readLe16(header + 10);
  status.fps = readLe16(header + 12);
  status.frameCount = readLe32(header + 16);
  status.indexOffset = readLe32(header + 20);
  status.dataOffset = readLe32(header + 24);
  uint32_t totalSize = readLe32(header + 28);
  status.maxFrameSize = readLe32(header + 32);
  status.crc32 = readLe32(header + 36);

  if (version != SDMJ_VERSION || headerSize != SDMJ_HEADER_SIZE) {
    file.close();
    setSdmjError(status, "BAD_VERSION");
    return false;
  }

  if (status.width == 0 || status.height == 0 ||
      status.width > SDMJ_WIDTH || status.height > SDMJ_HEIGHT ||
      SDMJ_WIDTH % status.width != 0 || SDMJ_HEIGHT % status.height != 0 ||
      status.fps == 0 || status.fps > 60) {
    file.close();
    setSdmjError(status, "BAD_GEOMETRY");
    return false;
  }

  if (status.frameCount == 0 || status.frameCount > SDMJ_MAX_FRAME_COUNT) {
    file.close();
    setSdmjError(status, "BAD_FRAME_COUNT");
    return false;
  }

  uint32_t expectedDataOffset =
      SDMJ_HEADER_SIZE + status.frameCount * SDMJ_INDEX_ENTRY_SIZE;
  if (status.indexOffset != SDMJ_HEADER_SIZE ||
      status.dataOffset != expectedDataOffset ||
      status.dataOffset > status.fileSize ||
      totalSize != status.fileSize) {
    file.close();
    setSdmjError(status, "BAD_OFFSETS");
    return false;
  }

  if (!file.seek(status.indexOffset)) {
    file.close();
    setSdmjError(status, "INDEX_SEEK");
    return false;
  }

  uint32_t expectedOffset = status.dataOffset;
  uint32_t largestFrame = 0;
  for (uint32_t i = 0; i < status.frameCount; i++) {
    uint8_t entry[SDMJ_INDEX_ENTRY_SIZE] = {0};
    if (!readExact(file, entry, sizeof(entry))) {
      file.close();
      setSdmjError(status, "INDEX_READ");
      return false;
    }

    uint32_t frameOffset = readLe32(entry);
    uint32_t frameSize = readLe32(entry + 4);
    if (frameSize == 0 || frameOffset != expectedOffset ||
        frameOffset + frameSize > status.fileSize ||
        frameOffset + frameSize < frameOffset) {
      file.close();
      setSdmjError(status, "BAD_FRAME");
      return false;
    }

    expectedOffset += frameSize;
    if (frameSize > largestFrame) {
      largestFrame = frameSize;
    }
  }

  if (expectedOffset != status.fileSize || largestFrame != status.maxFrameSize) {
    file.close();
    setSdmjError(status, "BAD_DATA");
    return false;
  }

  status.dataBytes = status.fileSize - status.dataOffset;
  status.ok = true;
  file.close();
  return true;
}

static void setSdraError(SdraFileStatus& status, const char* error) {
  snprintf(status.error, sizeof(status.error), "%s", error);
  status.ok = false;
}

static bool validateActiveRawScreensaver(SdraFileStatus& status) {
  memset(&status, 0, sizeof(status));

  if (!sdCardInitialized) {
    setSdraError(status, "SD_NOT_READY");
    return false;
  }

  if (!SD.exists(SCREENSAVER_ACTIVE_SDRA_PATH)) {
    setSdraError(status, "NOT_FOUND");
    return false;
  }

  File file = SD.open(SCREENSAVER_ACTIVE_SDRA_PATH, FILE_READ);
  if (!file) {
    setSdraError(status, "OPEN_FAILED");
    return false;
  }

  status.fileSize = static_cast<uint32_t>(file.size());
  if (status.fileSize < SDRA_HEADER_SIZE) {
    file.close();
    setSdraError(status, "TOO_SMALL");
    return false;
  }

  uint8_t header[SDRA_HEADER_SIZE] = {0};
  if (!readExact(file, header, sizeof(header))) {
    file.close();
    setSdraError(status, "HEADER_READ");
    return false;
  }

  if (memcmp(header, "SDRA", 4) != 0) {
    file.close();
    setSdraError(status, "BAD_MAGIC");
    return false;
  }

  uint16_t version = readLe16(header + 4);
  uint16_t headerSize = readLe16(header + 6);
  status.width = readLe16(header + 8);
  status.height = readLe16(header + 10);
  status.fps = readLe16(header + 12);
  status.tileSize = readLe16(header + 14);
  status.frameCount = readLe32(header + 16);
  status.indexOffset = readLe32(header + 20);
  status.dataOffset = readLe32(header + 24);
  uint32_t totalSize = readLe32(header + 28);
  status.maxFrameSize = readLe32(header + 32);
  status.crc32 = readLe32(header + 36);

  if (version != SDRA_VERSION || headerSize != SDRA_HEADER_SIZE) {
    file.close();
    setSdraError(status, "BAD_VERSION");
    return false;
  }

  if (status.width != SDMJ_WIDTH || status.height != SDMJ_HEIGHT ||
      status.fps == 0 || status.fps > 60 || status.tileSize == 0 ||
      status.width % status.tileSize != 0 ||
      status.height % status.tileSize != 0) {
    file.close();
    setSdraError(status, "BAD_GEOMETRY");
    return false;
  }

  if (status.frameCount == 0 || status.frameCount > SDMJ_MAX_FRAME_COUNT) {
    file.close();
    setSdraError(status, "BAD_FRAME_COUNT");
    return false;
  }

  status.tilesX = status.width / status.tileSize;
  status.tilesY = status.height / status.tileSize;
  status.fullFrameBytes =
      static_cast<uint32_t>(status.width) * status.height * sizeof(uint16_t);
  const uint32_t tileBytes =
      static_cast<uint32_t>(status.tileSize) * status.tileSize * sizeof(uint16_t);
  const uint32_t maxFullPayload = SDRA_FRAME_HEADER_SIZE + status.fullFrameBytes;
  const uint32_t maxTilePayload =
      SDRA_FRAME_HEADER_SIZE +
      static_cast<uint32_t>(status.tilesX) * status.tilesY *
          (sizeof(uint16_t) + tileBytes);

  uint32_t expectedDataOffset =
      SDRA_HEADER_SIZE + status.frameCount * SDRA_INDEX_ENTRY_SIZE;
  if (status.indexOffset != SDRA_HEADER_SIZE ||
      status.dataOffset != expectedDataOffset ||
      status.dataOffset > status.fileSize ||
      totalSize != status.fileSize ||
      status.maxFrameSize < SDRA_FRAME_HEADER_SIZE ||
      status.maxFrameSize > max(maxFullPayload, maxTilePayload)) {
    file.close();
    setSdraError(status, "BAD_OFFSETS");
    return false;
  }

  if (!file.seek(status.indexOffset)) {
    file.close();
    setSdraError(status, "INDEX_SEEK");
    return false;
  }

  uint32_t expectedOffset = status.dataOffset;
  uint32_t largestFrame = 0;
  for (uint32_t i = 0; i < status.frameCount; i++) {
    uint8_t entry[SDRA_INDEX_ENTRY_SIZE] = {0};
    if (!readExact(file, entry, sizeof(entry))) {
      file.close();
      setSdraError(status, "INDEX_READ");
      return false;
    }

    uint32_t frameOffset = readLe32(entry);
    uint32_t frameSize = readLe32(entry + 4);
    if (frameSize < SDRA_FRAME_HEADER_SIZE || frameOffset != expectedOffset ||
        frameOffset + frameSize > status.fileSize ||
        frameOffset + frameSize < frameOffset ||
        frameSize > max(maxFullPayload, maxTilePayload)) {
      file.close();
      setSdraError(status, "BAD_FRAME");
      return false;
    }

    expectedOffset += frameSize;
    if (frameSize > largestFrame) {
      largestFrame = frameSize;
    }
  }

  if (expectedOffset != status.fileSize || largestFrame != status.maxFrameSize) {
    file.close();
    setSdraError(status, "BAD_DATA");
    return false;
  }

  status.dataBytes = status.fileSize - status.dataOffset;
  status.ok = true;
  file.close();
  return true;
}

static void* allocateSdmjBuffer(size_t byteCount, bool preferInternal) {
  if (byteCount == 0) {
    return nullptr;
  }

  void* buffer = nullptr;
  if (preferInternal) {
    buffer = heap_caps_malloc(byteCount, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (buffer == nullptr) {
    buffer = heap_caps_malloc(byteCount, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }

  if (buffer == nullptr && !preferInternal) {
    buffer = heap_caps_malloc(byteCount, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (buffer == nullptr) {
    buffer = malloc(byteCount);
  }

  return buffer;
}

static uint32_t elapsedUs(uint32_t startedAtUs) {
  return static_cast<uint32_t>(micros() - startedAtUs);
}

static bool blitRgb565StripToFramebuffer(uint16_t* framebuffer,
                                         const uint16_t* stripPixels,
                                         uint16_t sourceWidth,
                                         uint16_t sourceHeight,
                                         uint16_t stripTop,
                                         uint16_t stripHeight,
                                         uint16_t scaleX,
                                         uint16_t scaleY,
                                         char* error,
                                         size_t errorSize) {
  if (framebuffer == nullptr || stripPixels == nullptr || sourceWidth == 0 ||
      sourceHeight == 0 || stripHeight == 0 || scaleX == 0 || scaleY == 0 ||
      sourceWidth * scaleX != SDMJ_WIDTH ||
      sourceHeight * scaleY != SDMJ_HEIGHT ||
      stripTop >= sourceHeight || stripTop + stripHeight > sourceHeight) {
    snprintf(error, errorSize, "BLIT_GEOMETRY");
    return false;
  }

  if (scaleX == 1 && scaleY == 1) {
    for (uint16_t row = 0; row < stripHeight; row++) {
      memcpy(framebuffer + (static_cast<uint32_t>(stripTop + row) * SDMJ_WIDTH),
             stripPixels + (static_cast<uint32_t>(row) * sourceWidth),
             static_cast<size_t>(sourceWidth) * sizeof(uint16_t));
    }
    return true;
  }

  for (uint16_t row = 0; row < stripHeight; row++) {
    const uint16_t* inputRow =
        stripPixels + (static_cast<uint32_t>(row) * sourceWidth);
    uint16_t* firstTargetRow =
        framebuffer +
        (static_cast<uint32_t>(stripTop + row) * scaleY * SDMJ_WIDTH);

    if (scaleX == 2) {
      for (uint16_t column = 0; column < sourceWidth; column++) {
        uint16_t pixel = inputRow[column];
        firstTargetRow[column * 2] = pixel;
        firstTargetRow[column * 2 + 1] = pixel;
      }
    } else {
      for (uint16_t column = 0; column < sourceWidth; column++) {
        uint16_t pixel = inputRow[column];
        uint16_t* targetPixel = firstTargetRow + column * scaleX;
        for (uint16_t repeatX = 0; repeatX < scaleX; repeatX++) {
          targetPixel[repeatX] = pixel;
        }
      }
    }

    size_t scaledRowBytes =
        static_cast<size_t>(sourceWidth) * scaleX * sizeof(uint16_t);
    for (uint16_t repeatY = 1; repeatY < scaleY; repeatY++) {
      memcpy(firstTargetRow + (static_cast<uint32_t>(repeatY) * SDMJ_WIDTH),
             firstTargetRow, scaledRowBytes);
    }
  }

  return true;
}

static bool readSdmjIndex(File& file, const SdmjFileStatus& status,
                          SdmjFrameEntry* frames, char* error,
                          size_t errorSize) {
  if (frames == nullptr) {
    snprintf(error, errorSize, "INDEX_ALLOC");
    return false;
  }

  if (!file.seek(status.indexOffset)) {
    snprintf(error, errorSize, "INDEX_SEEK");
    return false;
  }

  uint8_t entry[SDMJ_INDEX_ENTRY_SIZE] = {0};
  for (uint32_t i = 0; i < status.frameCount; i++) {
    if (!readExact(file, entry, sizeof(entry))) {
      snprintf(error, errorSize, "INDEX_READ");
      return false;
    }

    frames[i].offset = readLe32(entry);
    frames[i].size = readLe32(entry + 4);
  }

  return true;
}

static bool readSdraIndex(File& file, const SdraFileStatus& status,
                          SdmjFrameEntry* frames, char* error,
                          size_t errorSize) {
  if (frames == nullptr) {
    snprintf(error, errorSize, "INDEX_ALLOC");
    return false;
  }

  if (!file.seek(status.indexOffset)) {
    snprintf(error, errorSize, "INDEX_SEEK");
    return false;
  }

  uint8_t entry[SDRA_INDEX_ENTRY_SIZE] = {0};
  for (uint32_t i = 0; i < status.frameCount; i++) {
    if (!readExact(file, entry, sizeof(entry))) {
      snprintf(error, errorSize, "INDEX_READ");
      return false;
    }

    frames[i].offset = readLe32(entry);
    frames[i].size = readLe32(entry + 4);
  }

  return true;
}

static bool applySdraTile(uint16_t* framebuffer, const uint8_t* tilePixels,
                          const SdraFileStatus& status, uint16_t tileIndex) {
  if (framebuffer == nullptr || tilePixels == nullptr ||
      tileIndex >= static_cast<uint16_t>(status.tilesX * status.tilesY)) {
    return false;
  }

  const uint16_t tileX = tileIndex % status.tilesX;
  const uint16_t tileY = tileIndex / status.tilesX;
  const uint16_t tileSize = status.tileSize;
  const uint32_t tileRowBytes = static_cast<uint32_t>(tileSize) * sizeof(uint16_t);
  uint16_t* target =
      framebuffer +
      (static_cast<uint32_t>(tileY) * tileSize * status.width) +
      (static_cast<uint32_t>(tileX) * tileSize);

  for (uint16_t row = 0; row < tileSize; row++) {
    memcpy(target + static_cast<uint32_t>(row) * status.width,
           tilePixels + static_cast<uint32_t>(row) * tileRowBytes,
           tileRowBytes);
  }

  return true;
}

static bool applySdraFrameToBuffer(uint16_t* framebuffer,
                                   const uint8_t* payload,
                                   uint32_t payloadSize,
                                   const SdraFileStatus& status,
                                   uint16_t flags,
                                   uint16_t tileCount,
                                   char* error,
                                   size_t errorSize) {
  if (framebuffer == nullptr || payload == nullptr ||
      payloadSize < SDRA_FRAME_HEADER_SIZE) {
    snprintf(error, errorSize, "BAD_PAYLOAD");
    return false;
  }

  const uint8_t* cursor = payload + SDRA_FRAME_HEADER_SIZE;
  const uint32_t bodySize = payloadSize - SDRA_FRAME_HEADER_SIZE;
  const uint32_t tileBytes =
      static_cast<uint32_t>(status.tileSize) * status.tileSize * sizeof(uint16_t);

  if ((flags & SDRA_FRAME_FLAG_FULL) != 0) {
    if (bodySize != status.fullFrameBytes) {
      snprintf(error, errorSize, "BAD_FULL_FRAME");
      return false;
    }
    memcpy(framebuffer, cursor, status.fullFrameBytes);
    return true;
  }

  if (flags != 0) {
    snprintf(error, errorSize, "BAD_FLAGS");
    return false;
  }

  uint32_t expectedBodySize =
      static_cast<uint32_t>(tileCount) * (sizeof(uint16_t) + tileBytes);
  if (bodySize != expectedBodySize ||
      tileCount > static_cast<uint16_t>(status.tilesX * status.tilesY)) {
    snprintf(error, errorSize, "BAD_TILE_FRAME");
    return false;
  }

  for (uint16_t i = 0; i < tileCount; i++) {
    uint16_t tileIndex = readLe16(cursor);
    cursor += sizeof(uint16_t);
    if (!applySdraTile(framebuffer, cursor, status, tileIndex)) {
      snprintf(error, errorSize, "BAD_TILE_INDEX");
      return false;
    }
    cursor += tileBytes;
  }

  return true;
}

static bool screensaverTouchPressed() {
  if (g_touch == nullptr) {
    return false;
  }

  esp_panel::drivers::TouchPoint point;
  return g_touch->readPoints(&point, 1, 0) > 0;
}

static void waitForScreensaverTouchRelease() {
  if (g_touch == nullptr) {
    return;
  }

  uint32_t startedAtMs = millis();
  while (screensaverTouchPressed() && millis() - startedAtMs < 1000) {
    delay(20);
  }
}

static void paceSdmjFrame(uint32_t frameWorkUs, uint32_t frameIntervalUs,
                          SdmjPlaybackStats& stats) {
  if (frameWorkUs > stats.maxFrameWorkUs) {
    stats.maxFrameWorkUs = frameWorkUs;
  }

  if (frameWorkUs > frameIntervalUs) {
    stats.droppedFrames++;
    return;
  }

  uint32_t waitUs = frameIntervalUs - frameWorkUs;
  if (waitUs >= 2000) {
    delay(waitUs / 1000);
    waitUs %= 1000;
  }
  if (waitUs > 0) {
    delayMicroseconds(waitUs);
  }
}

static void restoreLvglScreenAfterSdmj() {
  lv_obj_invalidate(lv_scr_act());
}

static void sendSdmjPlaybackStats(CdcPortId sourcePort,
                                  const SdmjFileStatus& fileStatus,
                                  const SdmjPlaybackStats& stats,
                                  bool touchExit,
                                  const char* decoderName) {
  uint32_t fpsX100 = 0;
  if (stats.totalPlayUs > 0) {
    fpsX100 = static_cast<uint32_t>(
        (static_cast<uint64_t>(stats.framesPresented) * 100000000ULL) /
        stats.totalPlayUs);
  }

  uint64_t workUs =
      stats.totalReadUs + stats.totalDecodeUs + stats.totalSwitchUs;
  uint32_t rawFpsX100 = 0;
  if (workUs > 0) {
    rawFpsX100 = static_cast<uint32_t>(
        (static_cast<uint64_t>(stats.framesPresented) * 100000000ULL) / workUs);
  }

  uint32_t avgReadUs = stats.framesPresented > 0
                           ? static_cast<uint32_t>(stats.totalReadUs /
                                                   stats.framesPresented)
                           : 0;
  uint32_t avgDecodeUs = stats.framesPresented > 0
                             ? static_cast<uint32_t>(stats.totalDecodeUs /
                                                     stats.framesPresented)
                             : 0;
  uint32_t avgSwitchUs = stats.framesPresented > 0
                             ? static_cast<uint32_t>(stats.totalSwitchUs /
                                                     stats.framesPresented)
                             : 0;

  cdcReplyToPort(
      sourcePort,
      "CDC:SS PLAY ok=1 format=SDMJ decoder=%s target=%u frames=%u loops=%u fps=%u.%02u raw_fps=%u.%02u dropped=%u avg_read_us=%u avg_decode_us=%u avg_switch_us=%u max_frame_us=%u total_ms=%u touch=%u",
      decoderName != nullptr ? decoderName : "UNKNOWN", fileStatus.fps,
      static_cast<unsigned int>(stats.framesPresented),
      static_cast<unsigned int>(stats.loopsCompleted), fpsX100 / 100,
      fpsX100 % 100, rawFpsX100 / 100, rawFpsX100 % 100,
      static_cast<unsigned int>(stats.droppedFrames),
      static_cast<unsigned int>(avgReadUs), static_cast<unsigned int>(avgDecodeUs),
      static_cast<unsigned int>(avgSwitchUs),
      static_cast<unsigned int>(stats.maxFrameWorkUs),
      static_cast<unsigned int>(stats.totalPlayUs / 1000), touchExit ? 1 : 0);
}

static void sendSdraPlaybackStats(CdcPortId sourcePort,
                                  const SdraFileStatus& fileStatus,
                                  const SdmjPlaybackStats& stats,
                                  bool touchExit) {
  uint32_t fpsX100 = 0;
  if (stats.totalPlayUs > 0) {
    fpsX100 = static_cast<uint32_t>(
        (static_cast<uint64_t>(stats.framesPresented) * 100000000ULL) /
        stats.totalPlayUs);
  }

  uint64_t workUs =
      stats.totalReadUs + stats.totalDecodeUs + stats.totalSwitchUs;
  uint32_t rawFpsX100 = 0;
  if (workUs > 0) {
    rawFpsX100 = static_cast<uint32_t>(
        (static_cast<uint64_t>(stats.framesPresented) * 100000000ULL) / workUs);
  }

  uint32_t avgReadUs = stats.framesPresented > 0
                           ? static_cast<uint32_t>(stats.totalReadUs /
                                                   stats.framesPresented)
                           : 0;
  uint32_t avgApplyUs = stats.framesPresented > 0
                            ? static_cast<uint32_t>(stats.totalDecodeUs /
                                                    stats.framesPresented)
                            : 0;
  uint32_t avgSwitchUs = stats.framesPresented > 0
                             ? static_cast<uint32_t>(stats.totalSwitchUs /
                                                     stats.framesPresented)
                             : 0;

  cdcReplyToPort(
      sourcePort,
      "CDC:SS PLAY ok=1 format=SDRA target=%u frames=%u loops=%u fps=%u.%02u raw_fps=%u.%02u dropped=%u avg_read_us=%u avg_apply_us=%u avg_switch_us=%u max_frame_us=%u total_ms=%u touch=%u",
      fileStatus.fps, static_cast<unsigned int>(stats.framesPresented),
      static_cast<unsigned int>(stats.loopsCompleted), fpsX100 / 100,
      fpsX100 % 100, rawFpsX100 / 100, rawFpsX100 % 100,
      static_cast<unsigned int>(stats.droppedFrames),
      static_cast<unsigned int>(avgReadUs), static_cast<unsigned int>(avgApplyUs),
      static_cast<unsigned int>(avgSwitchUs),
      static_cast<unsigned int>(stats.maxFrameWorkUs),
      static_cast<unsigned int>(stats.totalPlayUs / 1000), touchExit ? 1 : 0);
}

static bool playActiveRawScreensaver(CdcPortId sourcePort,
                                     uint32_t requestedLoops,
                                     bool exitOnTouch,
                                     bool emitCdcStats) {
  SdraFileStatus fileStatus;
  if (!validateActiveRawScreensaver(fileStatus)) {
    if (emitCdcStats) {
      cdcReplyToPort(sourcePort, "CDC:SS PLAY ok=0 format=SDRA err=%s",
                     fileStatus.error);
    }
    return false;
  }

  char error[32] = {0};
  if (g_lcd == nullptr) {
    snprintf(error, sizeof(error), "LCD_NOT_READY");
  } else if (g_lcd->getFrameWidth() != SDMJ_WIDTH ||
             g_lcd->getFrameHeight() != SDMJ_HEIGHT ||
             g_lcd->getFrameColorBits() != 16) {
    snprintf(error, sizeof(error), "LCD_GEOMETRY");
  }

  uint16_t* frameBuffers[2] = {nullptr, nullptr};
  if (error[0] == '\0') {
    frameBuffers[0] =
        static_cast<uint16_t*>(g_lcd->getFrameBufferByIndex(0));
    frameBuffers[1] =
        static_cast<uint16_t*>(g_lcd->getFrameBufferByIndex(1));
    if (frameBuffers[0] == nullptr || frameBuffers[1] == nullptr) {
      snprintf(error, sizeof(error), "FB_UNAVAILABLE");
    }
  }

  File file;
  SdmjFrameEntry* frames = nullptr;
  uint8_t* framePayload = nullptr;

  if (error[0] == '\0') {
    file = SD.open(SCREENSAVER_ACTIVE_SDRA_PATH, FILE_READ);
    if (!file) {
      snprintf(error, sizeof(error), "OPEN_FAILED");
    }
  }

  if (error[0] == '\0') {
    frames = static_cast<SdmjFrameEntry*>(
        allocateSdmjBuffer(fileStatus.frameCount * sizeof(SdmjFrameEntry), true));
    framePayload =
        static_cast<uint8_t*>(allocateSdmjBuffer(fileStatus.maxFrameSize, false));

    if (frames == nullptr || framePayload == nullptr) {
      snprintf(error, sizeof(error), "ALLOC_FAILED");
    }
  }

  if (error[0] == '\0' &&
      !readSdraIndex(file, fileStatus, frames, error, sizeof(error))) {
    // readSdraIndex populated error.
  }

  if (error[0] != '\0') {
    if (file) {
      file.close();
    }
    if (frames != nullptr) {
      heap_caps_free(frames);
    }
    if (framePayload != nullptr) {
      heap_caps_free(framePayload);
    }
    if (emitCdcStats) {
      cdcReplyToPort(sourcePort, "CDC:SS PLAY ok=0 format=SDRA err=%s", error);
    }
    return false;
  }

  if (exitOnTouch) {
    waitForScreensaverTouchRelease();
  }

  bool locked = lvgl_port_lock(-1);
  if (!locked) {
    file.close();
    heap_caps_free(frames);
    heap_caps_free(framePayload);
    if (emitCdcStats) {
      cdcReplyToPort(sourcePort, "CDC:SS PLAY ok=0 format=SDRA err=LVGL_LOCK");
    }
    return false;
  }

  SdmjPlaybackStats stats;
  bool ok = true;
  bool touchExit = false;
  uint32_t activeBufferIndex = 0;
  uint32_t frameIntervalUs = 1000000UL / fileStatus.fps;
  uint32_t startedAtUs = micros();
  uint32_t loopsRemaining = requestedLoops == 0 ? 0xFFFFFFFFUL : requestedLoops;

  for (uint32_t loopIndex = 0; loopIndex < loopsRemaining && ok; loopIndex++) {
    if (!file.seek(frames[0].offset)) {
      snprintf(error, sizeof(error), "FRAME_SEEK");
      stats.decodeErrors++;
      ok = false;
      break;
    }

    for (uint32_t frameIndex = 0; frameIndex < fileStatus.frameCount; frameIndex++) {
      if (exitOnTouch && screensaverTouchPressed()) {
        touchExit = true;
        ok = false;
        break;
      }

      const SdmjFrameEntry& frame = frames[frameIndex];
      uint16_t* targetBuffer = frameBuffers[activeBufferIndex % 2];
      uint16_t* mirrorBuffer = frameBuffers[(activeBufferIndex + 1) % 2];
      activeBufferIndex++;

      uint32_t readStartedAtUs = micros();
      if (!file.seek(frame.offset) ||
          file.read(framePayload, frame.size) != frame.size) {
        snprintf(error, sizeof(error), "FRAME_READ");
        stats.decodeErrors++;
        ok = false;
        break;
      }
      uint32_t readUs = elapsedUs(readStartedAtUs);

      uint16_t flags = readLe16(framePayload);
      uint16_t tileCount = readLe16(framePayload + sizeof(uint16_t));

      uint32_t applyStartedAtUs = micros();
      if (!applySdraFrameToBuffer(targetBuffer, framePayload, frame.size,
                                  fileStatus, flags, tileCount, error,
                                  sizeof(error))) {
        stats.decodeErrors++;
        ok = false;
        break;
      }
      uint32_t targetApplyUs = elapsedUs(applyStartedAtUs);

      uint32_t switchStartedAtUs = micros();
      if (!g_lcd->switchFrameBufferTo(targetBuffer)) {
        snprintf(error, sizeof(error), "FB_SWITCH");
        ok = false;
        break;
      }
      uint32_t switchUs = elapsedUs(switchStartedAtUs);

      uint32_t mirrorApplyStartedAtUs = micros();
      if (!applySdraFrameToBuffer(mirrorBuffer, framePayload, frame.size,
                                  fileStatus, flags, tileCount, error,
                                  sizeof(error))) {
        stats.decodeErrors++;
        ok = false;
        break;
      }
      uint32_t applyUs = targetApplyUs + elapsedUs(mirrorApplyStartedAtUs);

      stats.framesPresented++;
      stats.totalReadUs += readUs;
      stats.totalDecodeUs += applyUs;
      stats.totalSwitchUs += switchUs;
      paceSdmjFrame(readUs + applyUs + switchUs, frameIntervalUs, stats);
    }

    if (!touchExit && ok) {
      stats.loopsCompleted++;
    }
  }

  stats.totalPlayUs = elapsedUs(startedAtUs);
  restoreLvglScreenAfterSdmj();
  lvgl_port_unlock();

  file.close();
  heap_caps_free(frames);
  heap_caps_free(framePayload);

  if (touchExit) {
    lastActivityTime = millis();
  }

  if (emitCdcStats) {
    if (ok || touchExit) {
      sendSdraPlaybackStats(sourcePort, fileStatus, stats, touchExit);
    } else {
      cdcReplyToPort(sourcePort,
                     "CDC:SS PLAY ok=0 format=SDRA err=%s frames=%u",
                     error[0] != '\0' ? error : "PLAY_FAILED",
                     static_cast<unsigned int>(stats.framesPresented));
    }
  }

  return ok || touchExit;
}

static bool playActiveScreensaverDirect(CdcPortId sourcePort,
                                        uint32_t requestedLoops,
                                        bool exitOnTouch,
                                        bool emitCdcStats) {
  SdmjFileStatus fileStatus;
  if (!validateActiveScreensaver(fileStatus)) {
    if (emitCdcStats) {
      cdcReplyToPort(sourcePort, "CDC:SS PLAY ok=0 err=%s", fileStatus.error);
    }
    return false;
  }

  char error[32] = {0};
  if (g_lcd == nullptr) {
    snprintf(error, sizeof(error), "LCD_NOT_READY");
  } else if (g_lcd->getFrameWidth() != SDMJ_WIDTH ||
             g_lcd->getFrameHeight() != SDMJ_HEIGHT ||
             g_lcd->getFrameColorBits() != 16) {
    snprintf(error, sizeof(error), "LCD_GEOMETRY");
  }

  void* frameBuffers[2] = {nullptr, nullptr};
  if (error[0] == '\0') {
    frameBuffers[0] = g_lcd->getFrameBufferByIndex(0);
    frameBuffers[1] = g_lcd->getFrameBufferByIndex(1);
    if (frameBuffers[0] == nullptr || frameBuffers[1] == nullptr) {
      snprintf(error, sizeof(error), "FB_UNAVAILABLE");
    }
  }

  File file;
  SdmjFrameEntry* frames = nullptr;
  uint8_t* jpegBuffer = nullptr;
  uint8_t* jpegBlockBuffer = nullptr;
  jpeg_dec_handle_t jpegDecoder = nullptr;
  uint16_t scaleX = 0;
  uint16_t scaleY = 0;
  bool useFullFrameDecode = false;

  if (error[0] == '\0') {
    file = SD.open(SCREENSAVER_ACTIVE_PATH, FILE_READ);
    if (!file) {
      snprintf(error, sizeof(error), "OPEN_FAILED");
    }
  }

  if (error[0] == '\0') {
    frames = static_cast<SdmjFrameEntry*>(
        allocateSdmjBuffer(fileStatus.frameCount * sizeof(SdmjFrameEntry), true));
    jpegBuffer =
        static_cast<uint8_t*>(allocateSdmjBuffer(fileStatus.maxFrameSize, true));
    useFullFrameDecode =
        fileStatus.width == SDMJ_WIDTH && fileStatus.height == SDMJ_HEIGHT &&
        ((reinterpret_cast<uintptr_t>(frameBuffers[0]) & 0x0F) == 0) &&
        ((reinterpret_cast<uintptr_t>(frameBuffers[1]) & 0x0F) == 0);
    if (!useFullFrameDecode) {
      size_t jpegBlockBytes =
          static_cast<size_t>(fileStatus.width) * 16 * sizeof(uint16_t);
      jpegBlockBuffer =
          static_cast<uint8_t*>(jpeg_calloc_align(jpegBlockBytes, 16));
    }

    jpeg_dec_config_t jpegConfig;
    memset(&jpegConfig, 0, sizeof(jpegConfig));
    jpegConfig.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpegConfig.rotate = JPEG_ROTATE_0D;
    jpegConfig.block_enable = !useFullFrameDecode;
    jpeg_error_t openResult = jpeg_dec_open(&jpegConfig, &jpegDecoder);

    if (frames == nullptr || jpegBuffer == nullptr ||
        (!useFullFrameDecode && jpegBlockBuffer == nullptr) ||
        openResult != JPEG_ERR_OK || jpegDecoder == nullptr) {
      snprintf(error, sizeof(error), "ALLOC_FAILED");
    }
  }

  if (error[0] == '\0' &&
      !readSdmjIndex(file, fileStatus, frames, error, sizeof(error))) {
    // readSdmjIndex populated error.
  }

  if (error[0] != '\0') {
    if (file) {
      file.close();
    }
    if (frames != nullptr) {
      heap_caps_free(frames);
    }
    if (jpegBuffer != nullptr) {
      heap_caps_free(jpegBuffer);
    }
    if (jpegBlockBuffer != nullptr) {
      jpeg_free_align(jpegBlockBuffer);
    }
    if (jpegDecoder != nullptr) {
      jpeg_dec_close(jpegDecoder);
    }
    if (emitCdcStats) {
      cdcReplyToPort(sourcePort, "CDC:SS PLAY ok=0 err=%s", error);
    }
    return false;
  }

  if (exitOnTouch) {
    waitForScreensaverTouchRelease();
  }

  bool locked = lvgl_port_lock(-1);
  if (!locked) {
    file.close();
    heap_caps_free(frames);
    heap_caps_free(jpegBuffer);
    jpeg_free_align(jpegBlockBuffer);
    jpeg_dec_close(jpegDecoder);
    if (emitCdcStats) {
      cdcReplyToPort(sourcePort, "CDC:SS PLAY ok=0 err=LVGL_LOCK");
    }
    return false;
  }

  SdmjPlaybackStats stats;
  bool ok = true;
  bool touchExit = false;
  uint32_t activeBufferIndex = 0;
  uint32_t frameIntervalUs = 1000000UL / fileStatus.fps;
  uint32_t startedAtUs = micros();
  uint32_t loopsRemaining = requestedLoops == 0 ? 0xFFFFFFFFUL : requestedLoops;
  scaleX = SDMJ_WIDTH / fileStatus.width;
  scaleY = SDMJ_HEIGHT / fileStatus.height;

  for (uint32_t loopIndex = 0; loopIndex < loopsRemaining && ok; loopIndex++) {
    if (loopIndex > 0) {
      file.close();
      file = SD.open(SCREENSAVER_ACTIVE_PATH, FILE_READ);
      if (!file) {
        snprintf(error, sizeof(error), "OPEN_FAILED");
        stats.decodeErrors++;
        ok = false;
        break;
      }
    }

    if (!file.seek(frames[0].offset)) {
      snprintf(error, sizeof(error), "FRAME_SEEK");
      stats.decodeErrors++;
      ok = false;
      break;
    }

    for (uint32_t frameIndex = 0; frameIndex < fileStatus.frameCount; frameIndex++) {
      if (exitOnTouch && screensaverTouchPressed()) {
        touchExit = true;
        ok = false;
        break;
      }

      const SdmjFrameEntry& frame = frames[frameIndex];
      void* targetBuffer = frameBuffers[activeBufferIndex % 2];
      activeBufferIndex++;

      uint32_t readStartedAtUs = micros();
      if (file.read(jpegBuffer, frame.size) != frame.size) {
        snprintf(error, sizeof(error), "FRAME_READ");
        stats.decodeErrors++;
        ok = false;
        break;
      }
      uint32_t readUs = elapsedUs(readStartedAtUs);

      uint32_t decodeStartedAtUs = micros();
      jpeg_dec_io_t jpegIo;
      memset(&jpegIo, 0, sizeof(jpegIo));
      jpegIo.inbuf = jpegBuffer;
      jpegIo.inbuf_len = static_cast<int>(frame.size);
      jpegIo.outbuf = jpegBlockBuffer;

      jpeg_dec_header_info_t jpegInfo;
      memset(&jpegInfo, 0, sizeof(jpegInfo));
      jpeg_error_t jpegResult =
          jpeg_dec_parse_header(jpegDecoder, &jpegIo, &jpegInfo);
      if (jpegResult != JPEG_ERR_OK) {
        snprintf(error, sizeof(error), "JPEG_HDR_%d", jpegResult);
        stats.decodeErrors++;
        ok = false;
        break;
      }

      if (jpegInfo.width != fileStatus.width ||
          jpegInfo.height != fileStatus.height) {
        snprintf(error, sizeof(error), "JPEG_SIZE");
        stats.decodeErrors++;
        ok = false;
        break;
      }

      int outputLen = 0;
      jpegResult = jpeg_dec_get_outbuf_len(jpegDecoder, &outputLen);
      if (jpegResult != JPEG_ERR_OK || outputLen <= 0 ||
          (!useFullFrameDecode &&
           outputLen > static_cast<int>(
                           static_cast<size_t>(fileStatus.width) * 16 *
                           sizeof(uint16_t))) ||
          (useFullFrameDecode &&
           outputLen > static_cast<int>(static_cast<size_t>(SDMJ_WIDTH) *
                                        SDMJ_HEIGHT * sizeof(uint16_t)))) {
        snprintf(error, sizeof(error), "JPEG_OBUF_%d", jpegResult);
        stats.decodeErrors++;
        ok = false;
        break;
      }

      int processCount = 0;
      jpegResult = jpeg_dec_get_process_count(jpegDecoder, &processCount);
      if (jpegResult != JPEG_ERR_OK || processCount <= 0) {
        snprintf(error, sizeof(error), "JPEG_COUNT_%d", jpegResult);
        stats.decodeErrors++;
        ok = false;
        break;
      }

      const uint32_t bytesPerRow =
          static_cast<uint32_t>(fileStatus.width) * sizeof(uint16_t);
      if (useFullFrameDecode) {
        jpegIo.outbuf = static_cast<uint8_t*>(targetBuffer);
        jpegIo.out_size = 0;
        jpegResult = jpeg_dec_process(jpegDecoder, &jpegIo);
        if (jpegResult != JPEG_ERR_OK ||
            jpegIo.out_size != outputLen ||
            outputLen != static_cast<int>(static_cast<size_t>(SDMJ_WIDTH) *
                                          SDMJ_HEIGHT * sizeof(uint16_t))) {
          snprintf(error, sizeof(error), "JPEG_DEC_%d", jpegResult);
          stats.decodeErrors++;
          ok = false;
          break;
        }
      } else {
        uint16_t stripTop = 0;
      uint8_t* directFrameOutput =
          (scaleX == 1 && scaleY == 1)
              ? static_cast<uint8_t*>(targetBuffer)
              : nullptr;
      for (int blockIndex = 0; blockIndex < processCount; blockIndex++) {
        jpegIo.outbuf =
            directFrameOutput != nullptr
                ? directFrameOutput +
                      static_cast<uint32_t>(stripTop) * bytesPerRow
                : jpegBlockBuffer;
        jpegIo.out_size = 0;
        jpegResult = jpeg_dec_process(jpegDecoder, &jpegIo);
        if (jpegResult != JPEG_ERR_OK) {
          snprintf(error, sizeof(error), "JPEG_DEC_%d", jpegResult);
          stats.decodeErrors++;
          ok = false;
          break;
        }

        if (jpegIo.out_size <= 0 ||
            (static_cast<uint32_t>(jpegIo.out_size) % bytesPerRow) != 0) {
          snprintf(error, sizeof(error), "JPEG_STRIP");
          stats.decodeErrors++;
          ok = false;
          break;
        }

        uint16_t stripHeight =
            static_cast<uint16_t>(jpegIo.out_size / bytesPerRow);
        if (directFrameOutput == nullptr) {
          if (!blitRgb565StripToFramebuffer(
                  static_cast<uint16_t*>(targetBuffer),
                  reinterpret_cast<const uint16_t*>(jpegBlockBuffer),
                  fileStatus.width, fileStatus.height, stripTop, stripHeight,
                  scaleX, scaleY, error, sizeof(error))) {
            stats.decodeErrors++;
            ok = false;
            break;
          }
        }
        stripTop += stripHeight;
      }

      if (!ok) {
        break;
      }

      if (stripTop != fileStatus.height) {
        snprintf(error, sizeof(error), "JPEG_ROWS");
        stats.decodeErrors++;
        ok = false;
        break;
      }
      }
      uint32_t decodeUs = elapsedUs(decodeStartedAtUs);

      uint32_t switchStartedAtUs = micros();
      if (!g_lcd->switchFrameBufferTo(targetBuffer)) {
        snprintf(error, sizeof(error), "FB_SWITCH");
        ok = false;
        break;
      }
      uint32_t switchUs = elapsedUs(switchStartedAtUs);

      stats.framesPresented++;
      stats.totalReadUs += readUs;
      stats.totalDecodeUs += decodeUs;
      stats.totalSwitchUs += switchUs;
      paceSdmjFrame(readUs + decodeUs + switchUs, frameIntervalUs, stats);
    }

    if (!touchExit && ok) {
      stats.loopsCompleted++;
    }
  }

  stats.totalPlayUs = elapsedUs(startedAtUs);
  restoreLvglScreenAfterSdmj();
  lvgl_port_unlock();

  file.close();
  heap_caps_free(frames);
  heap_caps_free(jpegBuffer);
  jpeg_free_align(jpegBlockBuffer);
  jpeg_dec_close(jpegDecoder);

  if (touchExit) {
    lastActivityTime = millis();
  }

  if (emitCdcStats) {
    if (ok || touchExit) {
      sendSdmjPlaybackStats(sourcePort, fileStatus, stats, touchExit,
                            useFullFrameDecode ? "ESP_NEW_JPEG_FULL"
                                               : "ESP_NEW_JPEG_BLOCK");
    } else {
      cdcReplyToPort(sourcePort, "CDC:SS PLAY ok=0 err=%s frames=%u",
                     error[0] != '\0' ? error : "PLAY_FAILED",
                     static_cast<unsigned int>(stats.framesPresented));
    }
  }

  return ok || touchExit;
}

static bool playActiveScreensaver(CdcPortId sourcePort,
                                  uint32_t requestedLoops,
                                  bool exitOnTouch,
                                  bool emitCdcStats) {
  if (sdCardInitialized && SD.exists(SCREENSAVER_ACTIVE_PATH)) {
    return playActiveScreensaverDirect(sourcePort, requestedLoops, exitOnTouch,
                                       emitCdcStats);
  }

  return playActiveRawScreensaver(sourcePort, requestedLoops, exitOnTouch,
                                  emitCdcStats);
}

static void clearMacroConfig() {
  for (int i = 0; i < TOTAL_BUTTONS; i++) {
    g_iconMacros[i].actionCount = 0;
    g_iconMacros[i].radialEnabled = false;
    for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
      g_iconMacros[i].radialItems[direction].actionCount = 0;
      g_iconMacros[i].radialItems[direction].configured = false;
    }
  }
}

static void addModifierFromName(const char* name, uint8_t& modifiers) {
  if (name == nullptr || name[0] == '\0') {
    return;
  }

  if (equalsIgnoreCase(name, "CTRL") || equalsIgnoreCase(name, "CONTROL")) {
    modifiers |= MacroModCtrl;
  } else if (equalsIgnoreCase(name, "SHIFT")) {
    modifiers |= MacroModShift;
  } else if (equalsIgnoreCase(name, "ALT") || equalsIgnoreCase(name, "OPTION")) {
    modifiers |= MacroModAlt;
  } else if (equalsIgnoreCase(name, "GUI") || equalsIgnoreCase(name, "WIN") ||
             equalsIgnoreCase(name, "CMD") || equalsIgnoreCase(name, "META")) {
    modifiers |= MacroModGui;
  }
}

static uint8_t parseModifiers(JsonVariantConst modifiersValue) {
  uint8_t modifiers = 0;

  if (modifiersValue.is<JsonArrayConst>()) {
    for (JsonVariantConst modValue : modifiersValue.as<JsonArrayConst>()) {
      addModifierFromName(modValue.as<const char*>(), modifiers);
    }
  } else if (modifiersValue.is<const char*>()) {
    addModifierFromName(modifiersValue.as<const char*>(), modifiers);
  }

  return modifiers;
}

static uint8_t parseMacroActions(JsonArrayConst actions, MacroAction* output,
                                 const char* contextLabel, int iconIndex) {
  if (actions.isNull() || output == nullptr) {
    return 0;
  }

  uint8_t actionCount = 0;
  for (JsonVariantConst actionValue : actions) {
    JsonObjectConst actionObj = actionValue.as<JsonObjectConst>();
    if (actionObj.isNull()) {
      continue;
    }

    if (actionCount >= MAX_ACTIONS_PER_ICON) {
      Serial.printf("%s %d has too many actions (max %d)\n", contextLabel, iconIndex,
                    MAX_ACTIONS_PER_ICON);
      break;
    }

    const char* actionType = actionObj["type"] | "";
    MacroAction action;

    if (equalsIgnoreCase(actionType, "combo")) {
      uint8_t keycode = 0;
      const char* keyName = actionObj["key"] | "";
      if (!keyNameToKeycode(keyName, keycode)) {
        Serial.printf("Invalid key '%s' on %s %d, action skipped\n", keyName,
                      contextLabel, iconIndex);
        continue;
      }

      action.type = MacroActionType::Combo;
      action.keycode = keycode;
      action.modifiers = parseModifiers(actionObj["mods"]);
    } else if (equalsIgnoreCase(actionType, "delay")) {
      int delayMs = actionObj["ms"] | 0;
      if (delayMs < 0) {
        delayMs = 0;
      }
      if (delayMs > 60000) {
        delayMs = 60000;
      }

      action.type = MacroActionType::Delay;
      action.delayMs = static_cast<uint16_t>(delayMs);
    } else {
      continue;
    }

    output[actionCount++] = action;
  }

  return actionCount;
}

static void ensureMacroConfigFileExists() {
  if (SD.exists(MACRO_CONFIG_PATH)) {
    return;
  }

  File file = SD.open(MACRO_CONFIG_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create macros.json");
    return;
  }

  file.println("{");
  file.println("  \"version\": 2,");
  file.println("  \"grid\": { \"rows\": 4, \"cols\": 8 },");
  file.println("  \"icons\": [");

  for (int index = 0; index < TOTAL_BUTTONS; index++) {
    int row = index / GRID_COLS;
    int col = index % GRID_COLS;

    file.print("    { \"index\": ");
    file.print(index);
    file.print(", \"row\": ");
    file.print(row);
    file.print(", \"col\": ");
    file.print(col);
    file.print(", \"actions\": [] }");
    if (index < TOTAL_BUTTONS - 1) {
      file.println(",");
    } else {
      file.println();
    }
  }

  file.println("  ]");
  file.println("}");
  file.close();

  Serial.println("Created default macros.json on SD card");
}

static void loadMacroConfigFromSD() {
  clearMacroConfig();

  if (!sdCardInitialized) {
    return;
  }

  ensureMacroConfigFileExists();

  File file = SD.open(MACRO_CONFIG_PATH, FILE_READ);
  if (!file) {
    Serial.println("Failed to open macros.json, macros disabled");
    return;
  }

  DynamicJsonDocument doc(24576);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("Failed to parse macros.json: ");
    Serial.println(error.c_str());
    return;
  }

  int version = doc["version"] | 0;
  if (version != 1 && version != MACRO_CONFIG_VERSION) {
    Serial.printf("macros.json version mismatch (got %d, expected 1 or %d)\n", version,
                  MACRO_CONFIG_VERSION);
  }

  JsonArrayConst icons = doc["icons"].as<JsonArrayConst>();
  if (icons.isNull()) {
    Serial.println("macros.json missing icons array");
    return;
  }

  bool seenIndexes[TOTAL_BUTTONS] = {false};
  for (JsonVariantConst iconValue : icons) {
    JsonObjectConst icon = iconValue.as<JsonObjectConst>();
    if (icon.isNull()) {
      continue;
    }

    int index = -1;
    if (icon["index"].is<int>()) {
      index = icon["index"].as<int>();
    } else if (icon["row"].is<int>() && icon["col"].is<int>()) {
      int row = icon["row"].as<int>();
      int col = icon["col"].as<int>();
      index = row * GRID_COLS + col;
    }

    if (index < 0 || index >= TOTAL_BUTTONS) {
      continue;
    }

    if (seenIndexes[index]) {
      Serial.printf("Duplicate macro icon index %d, later entry overrides earlier\n", index);
    }
    seenIndexes[index] = true;
    g_iconMacros[index].actionCount = 0;
    g_iconMacros[index].radialEnabled = false;
    for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
      g_iconMacros[index].radialItems[direction].actionCount = 0;
      g_iconMacros[index].radialItems[direction].configured = false;
    }

    JsonArrayConst actions = icon["actions"].as<JsonArrayConst>();
    g_iconMacros[index].actionCount =
        parseMacroActions(actions, g_iconMacros[index].actions, "Icon", index);

    JsonObjectConst radial = icon["radial"].as<JsonObjectConst>();
    if (!radial.isNull() && (radial["enabled"] | false)) {
      g_iconMacros[index].radialEnabled = true;
      JsonArrayConst radialItems = radial["items"].as<JsonArrayConst>();
      if (!radialItems.isNull()) {
        for (JsonVariantConst itemValue : radialItems) {
          JsonObjectConst item = itemValue.as<JsonObjectConst>();
          if (item.isNull()) {
            continue;
          }

          uint8_t directionIndex = 0;
          const char* directionName = item["direction"] | "";
          if (!radialDirectionIndexFromName(directionName, directionIndex)) {
            Serial.printf("Invalid radial direction '%s' on icon %d\n", directionName,
                          index);
            continue;
          }

          bool itemEnabled = item["enabled"] | true;
          RadialMacroItem& radialItem = g_iconMacros[index].radialItems[directionIndex];
          radialItem.actionCount = parseMacroActions(
              item["actions"].as<JsonArrayConst>(), radialItem.actions, "Radial item",
              index);

          JsonArrayConst hostActions = item["hostActions"].as<JsonArrayConst>();
          radialItem.configured =
              itemEnabled &&
              (radialItem.actionCount > 0 ||
               (!hostActions.isNull() && hostActions.size() > 0));
        }
      }

      g_iconMacros[index].radialEnabled = false;
      for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
        if (g_iconMacros[index].radialItems[direction].configured) {
          g_iconMacros[index].radialEnabled = true;
          break;
        }
      }
    }
  }

  int configuredIcons = 0;
  int totalActions = 0;
  int configuredRadialItems = 0;
  int missingIcons = 0;
  for (int i = 0; i < TOTAL_BUTTONS; i++) {
    if (g_iconMacros[i].actionCount > 0) {
      configuredIcons++;
      totalActions += g_iconMacros[i].actionCount;
    }
    for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
      if (g_iconMacros[i].radialItems[direction].configured) {
        configuredRadialItems++;
        totalActions += g_iconMacros[i].radialItems[direction].actionCount;
      }
    }
    if (!seenIndexes[i]) {
      missingIcons++;
    }
  }

  Serial.printf("Loaded macros.json: %d icons with actions, %d radial items, %d total actions\n",
                configuredIcons, configuredRadialItems, totalActions);
  if (missingIcons > 0) {
    Serial.printf("Warning: %d icon entries missing in macros.json\n", missingIcons);
  }
}

static void initUSBKeyboard() {
  if (usbKeyboardReady) {
    return;
  }

  if (expander != nullptr) {
    expander->pinMode(USB_SEL, OUTPUT);
    expander->digitalWrite(USB_SEL, LOW);
  }

  Serial.println("Initializing USB HID keyboard...");
  USB.productName("Finlay Deck");
  USB.manufacturerName("Finlay");

#if HAS_USB_CDC_CLASS
  cdcPort.begin();
  delay(10);
#endif

  keyboard.begin();
  delay(20);

  if (!USB.begin()) {
    Serial.println("USB initialization failed");
    return;
  }

  usbKeyboardReady = true;
  Serial.println("USB keyboard ready");

#if HAS_USB_CDC_CLASS
  usbCdcReady = true;
  cdcReplyToPort(CdcPortId::UsbCdcPort, "CDC:INFO USB_COMPOSITE_READY");
#endif
}

static void queueMacroActions(const MacroAction* actions, uint8_t actionCount,
                              const char* sourceLabel) {
  if (!usbKeyboardReady) {
    Serial.println("USB not ready, macro ignored");
    return;
  }

  if (actions == nullptr || actionCount == 0) {
    Serial.printf("%s has no macro actions\n", sourceLabel);
    return;
  }

  if (g_macroExecutor.active) {
    keyboard.releaseAll();
  }

  g_macroExecutor.active = true;
  g_macroExecutor.comboPressed = false;
  g_macroExecutor.actionIndex = 0;
  g_macroExecutor.actionCount = actionCount;
  g_macroExecutor.actions = actions;
  g_macroExecutor.nextActionAtMs = millis();

  Serial.printf("Queued macro for %s (%u actions)\n", sourceLabel, actionCount);
}

static void queueMacroForIcon(uint8_t iconIndex) {
  if (iconIndex >= TOTAL_BUTTONS) {
    return;
  }

  char label[24];
  snprintf(label, sizeof(label), "icon %u", iconIndex);
  queueMacroActions(g_iconMacros[iconIndex].actions, g_iconMacros[iconIndex].actionCount,
                    label);
}

static void tickMacroExecutor() {
  if (!usbKeyboardReady || !g_macroExecutor.active) {
    return;
  }

  unsigned long now = millis();
  if (now < g_macroExecutor.nextActionAtMs) {
    return;
  }

  if (g_macroExecutor.actions == nullptr ||
      g_macroExecutor.actionIndex >= g_macroExecutor.actionCount) {
    keyboard.releaseAll();
    g_macroExecutor.active = false;
    g_macroExecutor.comboPressed = false;
    g_macroExecutor.actions = nullptr;
    g_macroExecutor.actionCount = 0;
    return;
  }

  const MacroAction& action = g_macroExecutor.actions[g_macroExecutor.actionIndex];
  if (action.type == MacroActionType::Delay) {
    g_macroExecutor.actionIndex++;
    g_macroExecutor.nextActionAtMs = now + action.delayMs;
    return;
  }

  if (!g_macroExecutor.comboPressed) {
    if (action.modifiers & MacroModCtrl) {
      keyboard.press(KEY_LEFT_CTRL);
    }
    if (action.modifiers & MacroModShift) {
      keyboard.press(KEY_LEFT_SHIFT);
    }
    if (action.modifiers & MacroModAlt) {
      keyboard.press(KEY_LEFT_ALT);
    }
    if (action.modifiers & MacroModGui) {
      keyboard.press(KEY_LEFT_GUI);
    }

    keyboard.press(action.keycode);
    g_macroExecutor.comboPressed = true;
    g_macroExecutor.nextActionAtMs = now + kComboHoldMs;
  } else {
    keyboard.releaseAll();
    g_macroExecutor.comboPressed = false;
    g_macroExecutor.actionIndex++;
    g_macroExecutor.nextActionAtMs = now + kComboGapMs;
  }
}

static void resetCdcUploadState() {
  g_cdcUpload.active = false;
  g_cdcUpload.chunked = false;
  g_cdcUpload.targetPath[0] = '\0';
  g_cdcUpload.tempPath[0] = '\0';
  g_cdcUpload.expectedBytes = 0;
  g_cdcUpload.receivedBytes = 0;
  g_cdcUpload.chunkHeader[0] = 0;
  g_cdcUpload.chunkHeader[1] = 0;
  g_cdcUpload.chunkHeaderBytes = 0;
  g_cdcUpload.currentChunkSize = 0;
  g_cdcUpload.currentChunkBytes = 0;
  g_cdcUpload.lastDataMs = 0;
  g_cdcUpload.sourcePort = static_cast<uint8_t>(CdcPortId::SerialPort);
}

static CdcPortId activeUploadPortId() {
  return static_cast<CdcPortId>(g_cdcUpload.sourcePort);
}

static const char* activeUploadVerb() {
  return g_cdcUpload.chunked ? "PUTC" : "PUT";
}

static void abortCdcUpload(const char* reason) {
  CdcPortId sourcePort = activeUploadPortId();
  const char* verb = activeUploadVerb();

  if (g_cdcUpload.file) {
    g_cdcUpload.file.close();
  }

  if (g_cdcUpload.tempPath[0] != '\0' && SD.exists(g_cdcUpload.tempPath)) {
    SD.remove(g_cdcUpload.tempPath);
  }

  cdcReplyToPort(sourcePort, "CDC:ERR %s %s", verb, reason);
  resetCdcUploadState();
}

static void finishCdcUpload() {
  g_cdcUpload.file.close();

  if (SD.exists(g_cdcUpload.targetPath)) {
    SD.remove(g_cdcUpload.targetPath);
  }

  if (!SD.rename(g_cdcUpload.tempPath, g_cdcUpload.targetPath)) {
    abortCdcUpload("RENAME_FAILED");
    return;
  }

  CdcPortId sourcePort = activeUploadPortId();
  const char* verb = activeUploadVerb();

  if (strcmp(g_cdcUpload.targetPath, MACRO_CONFIG_PATH) == 0) {
    loadMacroConfigFromSD();
  }

  cdcReplyToPort(sourcePort, "CDC:OK %s %s %u", verb, g_cdcUpload.targetPath,
                 static_cast<unsigned int>(g_cdcUpload.receivedBytes));

  if (strcmp(g_cdcUpload.targetPath, MACRO_CONFIG_PATH) == 0) {
    cdcReplyToPort(sourcePort, "CDC:INFO RELOAD MACROS");
  } else if (isIconAssetPath(g_cdcUpload.targetPath)) {
    cdcReplyToPort(sourcePort, "CDC:INFO ICON STORED");
  }

  resetCdcUploadState();
}

static void beginCdcUpload(CdcPortId sourcePort, const char* targetPath,
                           size_t expectedBytes, bool chunked) {
  if (!sdCardInitialized) {
    cdcReplyToPort(sourcePort, "CDC:ERR SD_NOT_READY");
    return;
  }

  if (g_cdcUpload.active) {
    cdcReplyToPort(sourcePort, "CDC:ERR UPLOAD_ALREADY_ACTIVE");
    return;
  }

  if (!isAllowedUploadPath(targetPath)) {
    cdcReplyToPort(sourcePort, "CDC:ERR PATH_NOT_ALLOWED");
    return;
  }

  if (expectedBytes == 0 || expectedBytes > CDC_MAX_TRANSFER_BYTES) {
    cdcReplyToPort(sourcePort, "CDC:ERR INVALID_SIZE");
    return;
  }

  resetCdcUploadState();
  snprintf(g_cdcUpload.targetPath, sizeof(g_cdcUpload.targetPath), "%s",
           targetPath);
  snprintf(g_cdcUpload.tempPath, sizeof(g_cdcUpload.tempPath), "%s.tmp",
           targetPath);
  g_cdcUpload.expectedBytes = expectedBytes;

  if (SD.exists(g_cdcUpload.tempPath)) {
    SD.remove(g_cdcUpload.tempPath);
  }

  if (isScreensaverAssetPath(g_cdcUpload.targetPath) &&
      !SD.exists(SCREENSAVER_DIR_PATH)) {
    SD.mkdir(SCREENSAVER_DIR_PATH);
  }

  g_cdcUpload.file = SD.open(g_cdcUpload.tempPath, FILE_WRITE);
  if (!g_cdcUpload.file) {
    resetCdcUploadState();
    cdcReplyToPort(sourcePort, "CDC:ERR OPEN_TEMP_FAILED");
    return;
  }

  g_cdcUpload.active = true;
  g_cdcUpload.chunked = chunked;
  g_cdcUpload.receivedBytes = 0;
  g_cdcUpload.lastDataMs = millis();
  g_cdcUpload.sourcePort = static_cast<uint8_t>(sourcePort);

  cdcReplyToPort(sourcePort, "CDC:READY %s %s %u",
                 chunked ? "PUTC" : "PUT", g_cdcUpload.targetPath,
                 static_cast<unsigned int>(g_cdcUpload.expectedBytes));
}

static bool streamFileToPort(Stream* outputStream, File& file, size_t totalBytes) {
  if (outputStream == nullptr) {
    return false;
  }

  uint8_t chunk[256];
  size_t sentBytes = 0;

  while (sentBytes < totalBytes) {
    size_t remaining = totalBytes - sentBytes;
    size_t toRead = remaining;
    if (toRead > sizeof(chunk)) {
      toRead = sizeof(chunk);
    }

    size_t bytesRead = file.read(chunk, toRead);
    if (bytesRead == 0) {
      return false;
    }

    size_t bytesWritten = outputStream->write(chunk, bytesRead);
    if (bytesWritten != bytesRead) {
      return false;
    }

    sentBytes += bytesRead;
  }

  outputStream->flush();
  return true;
}

static void beginCdcDownload(CdcPortId sourcePort, const char* targetPath) {
  if (!sdCardInitialized) {
    cdcReplyToPort(sourcePort, "CDC:ERR SD_NOT_READY");
    return;
  }

  if (g_cdcUpload.active) {
    cdcReplyToPort(sourcePort, "CDC:ERR UPLOAD_ALREADY_ACTIVE");
    return;
  }

  if (!isAllowedUploadPath(targetPath)) {
    cdcReplyToPort(sourcePort, "CDC:ERR PATH_NOT_ALLOWED");
    return;
  }

  if (!SD.exists(targetPath)) {
    cdcReplyToPort(sourcePort, "CDC:ERR GET NOT_FOUND");
    return;
  }

  File file = SD.open(targetPath, FILE_READ);
  if (!file) {
    cdcReplyToPort(sourcePort, "CDC:ERR GET OPEN_FAILED");
    return;
  }

  size_t fileSize = file.size();
  if (fileSize > CDC_MAX_TRANSFER_BYTES) {
    file.close();
    cdcReplyToPort(sourcePort, "CDC:ERR GET INVALID_SIZE");
    return;
  }

  cdcReplyToPort(sourcePort, "CDC:READY GET %s %u", targetPath,
                 static_cast<unsigned int>(fileSize));

  Stream* outputStream = streamForPortId(sourcePort);
  if (!streamFileToPort(outputStream, file, fileSize)) {
    file.close();
    cdcReplyToPort(sourcePort, "CDC:ERR GET IO_FAILED");
    return;
  }

  file.close();
  cdcReplyToPort(sourcePort, "CDC:OK GET %s %u", targetPath,
                 static_cast<unsigned int>(fileSize));
}

static void sendScreensaverStatus(CdcPortId sourcePort) {
  if (sdCardInitialized && SD.exists(SCREENSAVER_ACTIVE_PATH)) {
    SdmjFileStatus status;
    if (!validateActiveScreensaver(status)) {
      cdcReplyToPort(sourcePort, "CDC:SS STATUS ok=0 format=SDMJ file=%s err=%s",
                     SCREENSAVER_ACTIVE_PATH, status.error);
      return;
    }

    cdcReplyToPort(
        sourcePort,
        "CDC:SS STATUS ok=1 format=SDMJ file=%s bytes=%u frames=%u width=%u height=%u fps=%u max_frame=%u data=%u crc=%08X",
        SCREENSAVER_ACTIVE_PATH, static_cast<unsigned int>(status.fileSize),
        static_cast<unsigned int>(status.frameCount), status.width, status.height,
        status.fps, static_cast<unsigned int>(status.maxFrameSize),
        static_cast<unsigned int>(status.dataBytes),
        static_cast<unsigned int>(status.crc32));
    return;
  }

  SdraFileStatus rawStatus;
  if (!validateActiveRawScreensaver(rawStatus)) {
    cdcReplyToPort(sourcePort, "CDC:SS STATUS ok=0 format=SDRA file=%s err=%s",
                   SCREENSAVER_ACTIVE_SDRA_PATH, rawStatus.error);
    return;
  }

  cdcReplyToPort(
      sourcePort,
      "CDC:SS STATUS ok=1 format=SDRA file=%s bytes=%u frames=%u width=%u height=%u fps=%u tile=%u max_frame=%u data=%u crc=%08X",
      SCREENSAVER_ACTIVE_SDRA_PATH, static_cast<unsigned int>(rawStatus.fileSize),
      static_cast<unsigned int>(rawStatus.frameCount), rawStatus.width,
      rawStatus.height, rawStatus.fps, rawStatus.tileSize,
      static_cast<unsigned int>(rawStatus.maxFrameSize),
      static_cast<unsigned int>(rawStatus.dataBytes),
      static_cast<unsigned int>(rawStatus.crc32));
}

static void handleCdcCommand(CdcPortId sourcePort, const char* line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  if (equalsIgnoreCase(line, "PING")) {
    cdcReplyToPort(sourcePort, "CDC:PONG");
    return;
  }

  if (equalsIgnoreCase(line, "HELP")) {
    cdcReplyToPort(sourcePort,
                   "CDC:CMDS PING|STATUS|SS STATUS|SS PLAY [loops]|RELOAD <MACROS|ICONS|ALL>|PUT <path> <size>|PUTC <path> <size>|GET <path>");
    return;
  }

  if (equalsIgnoreCase(line, "STATUS")) {
    int configuredIcons = 0;
    int configuredRadialItems = 0;
    for (int i = 0; i < TOTAL_BUTTONS; i++) {
      if (g_iconMacros[i].actionCount > 0) {
        configuredIcons++;
      }
      for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
        if (g_iconMacros[i].radialItems[direction].configured) {
          configuredRadialItems++;
        }
      }
    }

    cdcReplyToPort(sourcePort,
                   "CDC:STATUS sd=%d usb=%d macros=%d radial=%d active=%d events=1 proto=5",
                   sdCardInitialized ? 1 : 0, usbKeyboardReady ? 1 : 0,
                   configuredIcons, configuredRadialItems, g_macroExecutor.active ? 1 : 0);
    return;
  }

  if (equalsIgnoreCase(line, "SS STATUS")) {
    sendScreensaverStatus(sourcePort);
    return;
  }

  if (startsWithIgnoreCase(line, "SS PLAY")) {
    const char* loopsArg = line + 7;
    while (*loopsArg == ' ') {
      loopsArg++;
    }

    uint32_t loops = 1;
    if (*loopsArg != '\0') {
      char* endPtr = nullptr;
      unsigned long parsedLoops = strtoul(loopsArg, &endPtr, 10);
      while (endPtr != nullptr && *endPtr == ' ') {
        endPtr++;
      }
      if (endPtr == nullptr || *endPtr != '\0' || parsedLoops == 0 ||
          parsedLoops > 1000) {
        cdcReplyToPort(sourcePort, "CDC:ERR SS_PLAY_LOOPS");
        return;
      }
      loops = static_cast<uint32_t>(parsedLoops);
    }

    playActiveScreensaver(sourcePort, loops, false, true);
    return;
  }

  if (startsWithIgnoreCase(line, "RELOAD ")) {
    const char* target = line + 7;
    while (*target == ' ') {
      target++;
    }

    if (equalsIgnoreCase(target, "MACROS")) {
      loadMacroConfigFromSD();
      cdcReplyToPort(sourcePort, "CDC:OK RELOAD MACROS");
    } else if (equalsIgnoreCase(target, "ICONS")) {
      rebuildGridUI();
      cdcReplyToPort(sourcePort, "CDC:OK RELOAD ICONS");
    } else if (equalsIgnoreCase(target, "ALL")) {
      loadMacroConfigFromSD();
      rebuildGridUI();
      cdcReplyToPort(sourcePort, "CDC:OK RELOAD ALL");
    } else {
      cdcReplyToPort(sourcePort, "CDC:ERR RELOAD_TARGET");
    }
    return;
  }

  if (startsWithIgnoreCase(line, "PUT ")) {
    char path[64] = {0};
    unsigned long uploadSize = 0;
    if (sscanf(line, "PUT %63s %lu", path, &uploadSize) != 2) {
      cdcReplyToPort(sourcePort, "CDC:ERR PUT_SYNTAX");
      return;
    }

    beginCdcUpload(sourcePort, path, static_cast<size_t>(uploadSize), false);
    return;
  }

  if (startsWithIgnoreCase(line, "PUTC ")) {
    char path[64] = {0};
    unsigned long uploadSize = 0;
    if (sscanf(line, "PUTC %63s %lu", path, &uploadSize) != 2) {
      cdcReplyToPort(sourcePort, "CDC:ERR PUTC_SYNTAX");
      return;
    }

    beginCdcUpload(sourcePort, path, static_cast<size_t>(uploadSize), true);
    return;
  }

  if (startsWithIgnoreCase(line, "GET ")) {
    char path[64] = {0};
    if (sscanf(line, "GET %63s", path) != 1) {
      cdcReplyToPort(sourcePort, "CDC:ERR GET_SYNTAX");
      return;
    }

    beginCdcDownload(sourcePort, path);
    return;
  }

  cdcReplyToPort(sourcePort, "CDC:ERR UNKNOWN_CMD");
}

static bool uploadBelongsToPort(CdcPortId portId) {
  return g_cdcUpload.active &&
         (static_cast<CdcPortId>(g_cdcUpload.sourcePort) == portId);
}

static void processChunkedCdcUpload(Stream* inputStream) {
  uint8_t chunk[CDC_UPLOAD_BUFFER_BYTES];

  while (inputStream->available() > 0 && g_cdcUpload.active) {
    if (g_cdcUpload.chunkHeaderBytes < sizeof(g_cdcUpload.chunkHeader)) {
      int rawByte = inputStream->read();
      if (rawByte < 0) {
        break;
      }

      g_cdcUpload.chunkHeader[g_cdcUpload.chunkHeaderBytes++] =
          static_cast<uint8_t>(rawByte);
      g_cdcUpload.lastDataMs = millis();

      if (g_cdcUpload.chunkHeaderBytes < sizeof(g_cdcUpload.chunkHeader)) {
        continue;
      }

      g_cdcUpload.currentChunkSize = readLe16(g_cdcUpload.chunkHeader);
      g_cdcUpload.currentChunkBytes = 0;

      size_t remaining =
          g_cdcUpload.expectedBytes - g_cdcUpload.receivedBytes;
      if (g_cdcUpload.currentChunkSize == 0 ||
          g_cdcUpload.currentChunkSize > sizeof(chunk) ||
          g_cdcUpload.currentChunkSize > remaining) {
        abortCdcUpload("BAD_CHUNK");
        break;
      }

      if (inputStream->available() <= 0) {
        continue;
      }
    }

    size_t chunkRemaining =
        g_cdcUpload.currentChunkSize - g_cdcUpload.currentChunkBytes;
    size_t toRead = static_cast<size_t>(inputStream->available());
    if (toRead > chunkRemaining) {
      toRead = chunkRemaining;
    }
    if (toRead > sizeof(chunk)) {
      toRead = sizeof(chunk);
    }

    size_t bytesRead =
        inputStream->readBytes(reinterpret_cast<char*>(chunk), toRead);
    if (bytesRead == 0) {
      break;
    }

    size_t bytesWritten = g_cdcUpload.file.write(chunk, bytesRead);
    if (bytesWritten != bytesRead) {
      abortCdcUpload("WRITE_FAILED");
      break;
    }

    g_cdcUpload.currentChunkBytes += bytesWritten;
    g_cdcUpload.receivedBytes += bytesWritten;
    g_cdcUpload.lastDataMs = millis();

    if (g_cdcUpload.currentChunkBytes >= g_cdcUpload.currentChunkSize) {
      g_cdcUpload.chunkHeaderBytes = 0;
      g_cdcUpload.currentChunkSize = 0;
      g_cdcUpload.currentChunkBytes = 0;

      if (g_cdcUpload.receivedBytes >= g_cdcUpload.expectedBytes) {
        finishCdcUpload();
        break;
      }

      cdcReplyToPort(activeUploadPortId(), "CDC:ACK PUTC %u",
                     static_cast<unsigned int>(g_cdcUpload.receivedBytes));
    }
  }
}

static void processCdcInputForPort(CdcPortId portId, Stream* inputStream,
                                   char* lineBuffer,
                                   size_t* lineLength) {
  while (inputStream->available() > 0) {
    if (uploadBelongsToPort(portId)) {
      if (g_cdcUpload.chunked) {
        processChunkedCdcUpload(inputStream);
        continue;
      }

      uint8_t chunk[CDC_UPLOAD_BUFFER_BYTES];
      size_t remaining = g_cdcUpload.expectedBytes - g_cdcUpload.receivedBytes;
      size_t toRead = static_cast<size_t>(inputStream->available());
      if (toRead > remaining) {
        toRead = remaining;
      }
      if (toRead > sizeof(chunk)) {
        toRead = sizeof(chunk);
      }

      size_t bytesRead =
          inputStream->readBytes(reinterpret_cast<char*>(chunk), toRead);
      if (bytesRead == 0) {
        break;
      }

      size_t bytesWritten = g_cdcUpload.file.write(chunk, bytesRead);
      if (bytesWritten != bytesRead) {
        abortCdcUpload("WRITE_FAILED");
        break;
      }

      g_cdcUpload.receivedBytes += bytesWritten;
      g_cdcUpload.lastDataMs = millis();

      if (g_cdcUpload.receivedBytes >= g_cdcUpload.expectedBytes) {
        finishCdcUpload();
      }

      continue;
    }

    int rawByte = inputStream->read();
    if (rawByte < 0) {
      break;
    }

    char c = static_cast<char>(rawByte);
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      lineBuffer[*lineLength] = '\0';
      handleCdcCommand(portId, lineBuffer);
      *lineLength = 0;
      lineBuffer[0] = '\0';
      continue;
    }

    if (*lineLength >= 159) {
      *lineLength = 0;
      lineBuffer[0] = '\0';
      cdcReplyToPort(portId, "CDC:ERR LINE_TOO_LONG");
      continue;
    }

    lineBuffer[(*lineLength)++] = c;
  }
}

void processCdcInput() {
  if (g_cdcUpload.active &&
      (millis() - g_cdcUpload.lastDataMs > kCdcUploadTimeoutMs)) {
    char reason[48];
    snprintf(reason, sizeof(reason), "TIMEOUT %u/%u",
             static_cast<unsigned int>(g_cdcUpload.receivedBytes),
             static_cast<unsigned int>(g_cdcUpload.expectedBytes));
    abortCdcUpload(reason);
  }

  processCdcInputForPort(CdcPortId::SerialPort, &Serial, g_serialLineBuffer,
                         &g_serialLineLength);

#if HAS_USB_CDC_CLASS
  if (usbCdcReady) {
    processCdcInputForPort(CdcPortId::UsbCdcPort, &cdcPort, g_usbLineBuffer,
                           &g_usbLineLength);
  }
#endif
}

// Screensaver globals
unsigned long lastActivityTime = 0;
bool isScreensaverActive = false;
lv_obj_t* screensaverObj = nullptr;

// Reset inactivity timer (and exit screensaver if active)
void resetActivityTimer() {
  lastActivityTime = millis();
  if (isScreensaverActive) {
    deactivateScreensaver();
  }
}

// Activate screensaver
void activateScreensaver() {
  if (isScreensaverActive) return;

  Serial.println("Activating screensaver");

  if (sdCardInitialized &&
      (SD.exists(SCREENSAVER_ACTIVE_SDRA_PATH) ||
       SD.exists(SCREENSAVER_ACTIVE_PATH))) {
    if (expander != nullptr) {
      expander->digitalWrite(LCD_BL, HIGH);
    }

    isScreensaverActive = true;
    bool played = playActiveScreensaver(CdcPortId::SerialPort, 0, true, false);
    isScreensaverActive = false;
    if (played) {
      Serial.println("Screensaver playback exited");
      return;
    }

    Serial.println("Animated screensaver unavailable, falling back to blank screen");
  }

  // Turn off backlight to save power
  if (expander != nullptr) {
    expander->digitalWrite(LCD_BL, LOW);
  }

  lvgl_port_lock(-1);

  // Full-screen black overlay
  screensaverObj = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(screensaverObj);
  lv_obj_set_size(screensaverObj, 800, 480);
  lv_obj_set_pos(screensaverObj, 0, 0);
  lv_obj_set_style_bg_color(screensaverObj, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screensaverObj, LV_OPA_COVER, 0);

  // Any touch will reset the timer
  lv_obj_add_event_cb(
      screensaverObj,
      [](lv_event_t* e) {
        resetActivityTimer();
      },
      LV_EVENT_CLICKED, nullptr);

  lv_obj_add_event_cb(
      screensaverObj,
      [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
          resetActivityTimer();
        }
      },
      LV_EVENT_ALL, nullptr);

  isScreensaverActive = true;
  lvgl_port_unlock();
}

// Deactivate screensaver
void deactivateScreensaver() {
  if (!isScreensaverActive) return;

  Serial.println("Deactivating screensaver");

  // Backlight on
  if (expander != nullptr) {
    expander->digitalWrite(LCD_BL, HIGH);
  }

  lvgl_port_lock(-1);

  if (screensaverObj != nullptr) {
    lv_obj_del(screensaverObj);
    screensaverObj = nullptr;
  }

  isScreensaverActive = false;
  lvgl_port_unlock();
}

lv_obj_t* create_icon_image(const char* path) {
  lv_obj_t* img = nullptr;
  lv_img_dsc_t* img_dsc = nullptr;
  const size_t expectedIconBytes =
      static_cast<size_t>(BUTTON_SIZE) * BUTTON_SIZE * sizeof(lv_color_t);

  Serial.print("Trying to load icon from path: ");
  Serial.println(path);

  if (sdCardInitialized) {
    File file = SD.open(path);
    if (file) {
      size_t size = file.size();
      Serial.print("File opened successfully, size: ");
      Serial.println(size);

      if (size < expectedIconBytes) {
        Serial.println("Icon file too small, using fallback");
        file.close();
      } else {

        img_dsc = (lv_img_dsc_t*)heap_caps_malloc(
            sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint8_t* icon_data = (uint8_t*)heap_caps_malloc(
            size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

        if (icon_data && img_dsc) {
          (void)file.read(icon_data, size);

          memset(img_dsc, 0, sizeof(lv_img_dsc_t));
          img_dsc->data = icon_data;
          img_dsc->data_size = size;
          img_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
          img_dsc->header.w = BUTTON_SIZE;
          img_dsc->header.h = BUTTON_SIZE;

          img = lv_img_create(lv_scr_act());
          if (img) {
            lv_obj_set_user_data(img, img_dsc);
            lv_img_set_src(img, img_dsc);

            lv_obj_add_event_cb(
                img,
                [](lv_event_t* e) {
                  lv_obj_t* obj = lv_event_get_target(e);
                  auto* desc =
                      (lv_img_dsc_t*)lv_obj_get_user_data(obj);
                  if (desc) {
                    if (desc->data) {
                      heap_caps_free((void*)desc->data);
                    }
                    heap_caps_free(desc);
                  }
                },
                LV_EVENT_DELETE, nullptr);
          } else {
            heap_caps_free(icon_data);
            heap_caps_free(img_dsc);
          }
        } else {
          if (icon_data) heap_caps_free(icon_data);
          if (img_dsc) heap_caps_free(img_dsc);
        }
        file.close();
      }
    }
  }

  if (img == nullptr) {
    // Fallback icon
    Serial.println("Loading fallback icon...");
    File fallback = SD.open(FALLBACK_ICON_PATH);
    if (fallback) {
      size_t size = fallback.size();
      if (size < expectedIconBytes) {
        Serial.println("Fallback icon too small");
        fallback.close();
        return nullptr;
      }

      auto* fallback_dsc = (lv_img_dsc_t*)heap_caps_malloc(
          sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      uint8_t* fallback_data = (uint8_t*)heap_caps_malloc(
          size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

      if (fallback_data && fallback_dsc) {
        (void)fallback.read(fallback_data, size);

        memset(fallback_dsc, 0, sizeof(lv_img_dsc_t));
        fallback_dsc->data = fallback_data;
        fallback_dsc->data_size = size;
        fallback_dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
        fallback_dsc->header.w = BUTTON_SIZE;
        fallback_dsc->header.h = BUTTON_SIZE;

        img = lv_img_create(lv_scr_act());
        if (img) {
          lv_obj_set_user_data(img, fallback_dsc);
          lv_img_set_src(img, fallback_dsc);

          lv_obj_add_event_cb(
              img,
              [](lv_event_t* e) {
                lv_obj_t* obj = lv_event_get_target(e);
                auto* desc =
                    (lv_img_dsc_t*)lv_obj_get_user_data(obj);
                if (desc) {
                  if (desc->data) {
                    heap_caps_free((void*)desc->data);
                  }
                  heap_caps_free(desc);
                }
              },
              LV_EVENT_DELETE, nullptr);
        } else {
          heap_caps_free(fallback_data);
          heap_caps_free(fallback_dsc);
        }
      } else {
        if (fallback_data) heap_caps_free(fallback_data);
        if (fallback_dsc) heap_caps_free(fallback_dsc);
      }
      fallback.close();
    }
  }

  return img;
}

static bool getActivePointerPoint(lv_point_t& point) {
  lv_indev_t* indev = lv_indev_get_act();
  if (indev == nullptr) {
    return false;
  }

  lv_indev_get_point(indev, &point);
  return true;
}

static int radialDirectionFromPoint(const lv_point_t& point) {
  int dx = point.x - g_radialMenu.origin.x;
  int dy = point.y - g_radialMenu.origin.y;
  int maxDelta = max(abs(dx), abs(dy));
  if (maxDelta < RADIAL_MENU_DEADZONE_PX) {
    return -1;
  }

  if (abs(dx) > abs(dy)) {
    return dx > 0 ? 1 : 3;
  }

  return dy > 0 ? 2 : 0;
}

static bool radialDragExceededOpenThreshold(const lv_point_t& point) {
  if (!g_radialMenu.hasPressPoint) {
    return false;
  }

  int dx = point.x - g_radialMenu.pressPoint.x;
  int dy = point.y - g_radialMenu.pressPoint.y;
  return max(abs(dx), abs(dy)) >= RADIAL_MENU_OPEN_DRAG_PX;
}

static int radialDistanceFromOrigin(const lv_point_t& point) {
  int dx = point.x - g_radialMenu.origin.x;
  int dy = point.y - g_radialMenu.origin.y;
  return max(abs(dx), abs(dy));
}

static void resetRadialGestureTracking() {
  g_radialMenu.bestPoint = g_radialMenu.pressPoint;
  g_radialMenu.bestDistance = 0;
  g_radialMenu.selectedDirection = -1;
}

static void suppressSourceButtonTapFeedback(lv_obj_t* btn) {
  if (btn == nullptr) {
    return;
  }

  lv_anim_del(btn, nullptr);
  lv_obj_set_style_transform_zoom(btn, LV_IMG_ZOOM_NONE, LV_STATE_PRESSED);
  lv_obj_clear_state(btn, LV_STATE_PRESSED);
  lv_obj_invalidate(btn);
}

static void restoreSourceButtonTapFeedback() {
  lv_obj_t* btn = g_radialMenu.sourceButton;
  g_radialMenu.sourceButton = nullptr;

  if (btn == nullptr) {
    return;
  }

  lv_obj_clear_state(btn, LV_STATE_PRESSED);
  lv_obj_remove_local_style_prop(btn, LV_STYLE_TRANSFORM_ZOOM, LV_STATE_PRESSED);
  lv_obj_invalidate(btn);
}

static void updateRadialSelection(int selectedDirection) {
  g_radialMenu.selectedDirection = selectedDirection;

  for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
    lv_obj_t* item = g_radialMenu.itemObjects[direction];
    if (item == nullptr) {
      continue;
    }

    bool configured =
        g_iconMacros[g_radialMenu.iconIndex].radialItems[direction].configured;
    bool selected = selectedDirection == direction;
    lv_obj_set_style_border_width(item, selected ? 3 : 1, 0);
    lv_obj_set_style_border_color(
        item, lv_color_hex(selected ? 0xFFFFFF : configured ? 0x4C4C4C : 0x272727),
        0);
    lv_obj_set_style_bg_color(
        item, lv_color_hex(selected ? 0x2E7D70 : configured ? 0x101010 : 0x050505),
        0);
    lv_obj_set_style_bg_opa(item, configured ? LV_OPA_COVER : LV_OPA_50, 0);
  }
}

static void updateRadialGestureCandidate(const lv_point_t& point) {
  int distance = radialDistanceFromOrigin(point);
  if (distance < g_radialMenu.bestDistance) {
    return;
  }

  g_radialMenu.bestPoint = point;
  g_radialMenu.bestDistance = distance;
  updateRadialSelection(radialDirectionFromPoint(g_radialMenu.bestPoint));
}

static void hideRadialMenu() {
  restoreSourceButtonTapFeedback();

  if (g_radialMenu.overlay != nullptr) {
    lv_obj_del(g_radialMenu.overlay);
  }

  g_radialMenu.active = false;
  g_radialMenu.overlay = nullptr;
  g_radialMenu.selectedDirection = -1;
  for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
    g_radialMenu.itemObjects[direction] = nullptr;
  }
}

static void showRadialMenu(uint8_t iconIndex, int row, int col, lv_obj_t* sourceButton,
                           const lv_point_t& origin);

static void maybeOpenRadialMenuFromDrag(uint8_t iconIndex, int row, int col,
                                        lv_obj_t* sourceButton) {
  if (g_radialMenu.active || iconIndex >= TOTAL_BUTTONS ||
      !g_iconMacros[iconIndex].radialEnabled) {
    return;
  }

  lv_point_t point;
  if (!getActivePointerPoint(point) || !radialDragExceededOpenThreshold(point)) {
    return;
  }

  showRadialMenu(iconIndex, row, col, sourceButton, g_radialMenu.origin);
  updateRadialGestureCandidate(point);
}

static void showRadialMenu(uint8_t iconIndex, int row, int col, lv_obj_t* sourceButton,
                           const lv_point_t& origin) {
  if (iconIndex >= TOTAL_BUTTONS || !g_iconMacros[iconIndex].radialEnabled) {
    return;
  }

  hideRadialMenu();

  g_radialMenu.active = true;
  g_radialMenu.suppressNextClick = true;
  g_radialMenu.iconIndex = iconIndex;
  g_radialMenu.row = row;
  g_radialMenu.col = col;
  g_radialMenu.origin = origin;
  resetRadialGestureTracking();
  g_radialMenu.sourceButton = sourceButton;
  suppressSourceButtonTapFeedback(g_radialMenu.sourceButton);

  lv_obj_t* screen = lv_scr_act();
  g_radialMenu.overlay = lv_obj_create(screen);
  lv_obj_remove_style_all(g_radialMenu.overlay);
  lv_obj_set_size(g_radialMenu.overlay, 800, 480);
  lv_obj_set_pos(g_radialMenu.overlay, 0, 0);
  lv_obj_set_style_bg_color(g_radialMenu.overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_radialMenu.overlay, LV_OPA_50, 0);
  lv_obj_clear_flag(g_radialMenu.overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(g_radialMenu.overlay);

  int menuSize = BUTTON_SIZE * 3 + RADIAL_MENU_GAP * 2;
  int menuX = origin.x - menuSize / 2;
  int menuY = origin.y - menuSize / 2;
  menuX = max(0, min(800 - menuSize, menuX));
  menuY = max(0, min(480 - menuSize, menuY));

  for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
    int offsetX = 0;
    int offsetY = 0;
    if (!radialDirectionOffset(direction, offsetX, offsetY)) {
      continue;
    }

    if (!g_iconMacros[iconIndex].radialItems[direction].configured) {
      continue;
    }

    int itemX = menuX + (offsetX + 1) * (BUTTON_SIZE + RADIAL_MENU_GAP);
    int itemY = menuY + (offsetY + 1) * (BUTTON_SIZE + RADIAL_MENU_GAP);

    lv_obj_t* item = lv_obj_create(g_radialMenu.overlay);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, BUTTON_SIZE, BUTTON_SIZE);
    lv_obj_set_pos(item, itemX, itemY);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(0x4C4C4C), 0);
    lv_obj_set_style_radius(item, 0, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_CLICKABLE);
    g_radialMenu.itemObjects[direction] = item;

    char radialPath[40];
    snprintf(radialPath, sizeof(radialPath), RADIAL_ICON_FORMAT, row, col,
             radialDirectionName(direction));
    lv_obj_t* icon = create_icon_image(radialPath);
    if (icon) {
      lv_obj_set_parent(icon, item);
      lv_obj_center(icon);
      lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    } else {
      lv_obj_t* label = lv_label_create(item);
      lv_label_set_text(label, radialDirectionName(direction));
      lv_obj_set_style_text_color(label, lv_color_hex(0xD0D0D0), 0);
      lv_obj_center(label);
    }
  }

  lv_obj_t* center = lv_obj_create(g_radialMenu.overlay);
  lv_obj_remove_style_all(center);
  lv_obj_set_size(center, BUTTON_SIZE, BUTTON_SIZE);
  lv_obj_set_pos(center, menuX + BUTTON_SIZE + RADIAL_MENU_GAP,
                 menuY + BUTTON_SIZE + RADIAL_MENU_GAP);
  lv_obj_set_style_bg_color(center, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(center, 1, 0);
  lv_obj_set_style_border_color(center, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(center, 0, 0);
  lv_obj_clear_flag(center, LV_OBJ_FLAG_CLICKABLE);

  char centerIconPath[32];
  snprintf(centerIconPath, sizeof(centerIconPath), ICON_FORMAT, row, col);
  lv_obj_t* centerIcon = create_icon_image(centerIconPath);
  if (centerIcon) {
    lv_obj_set_parent(centerIcon, center);
    lv_obj_center(centerIcon);
    lv_obj_clear_flag(centerIcon, LV_OBJ_FLAG_CLICKABLE);
  }

  updateRadialSelection(g_radialMenu.selectedDirection);
}

static void updateRadialMenuFromTouch() {
  if (!g_radialMenu.active) {
    return;
  }

  lv_point_t point;
  if (!getActivePointerPoint(point)) {
    return;
  }

  updateRadialGestureCandidate(point);
}

static bool executeRadialDirection(uint8_t iconIndex, int row, int col,
                                   int selectedDirection) {
  if (selectedDirection < 0 || selectedDirection >= RADIAL_DIRECTION_COUNT) {
    return false;
  }

  if (iconIndex >= TOTAL_BUTTONS) {
    return false;
  }

  RadialMacroItem& item = g_iconMacros[iconIndex].radialItems[selectedDirection];
  if (!item.configured) {
    return false;
  }

  const char* directionName = radialDirectionName(selectedDirection);
  Serial.printf("Radial selected: icon=%u direction=%s\n", iconIndex, directionName);
  emitRadialEvent(iconIndex, row, col, directionName);

  char label[32];
  snprintf(label, sizeof(label), "radial %u %s", iconIndex, directionName);
  queueMacroActions(item.actions, item.actionCount, label);
  return true;
}

static void executeRadialMenuSelection() {
  if (!g_radialMenu.active) {
    return;
  }

  executeRadialDirection(g_radialMenu.iconIndex, g_radialMenu.row, g_radialMenu.col,
                         g_radialMenu.selectedDirection);
}

static bool handleFastRadialRelease(uint8_t iconIndex, int row, int col) {
  if (iconIndex >= TOTAL_BUTTONS || !g_iconMacros[iconIndex].radialEnabled) {
    return false;
  }

  lv_point_t point;
  if (!getActivePointerPoint(point) || !radialDragExceededOpenThreshold(point)) {
    return false;
  }

  updateRadialGestureCandidate(point);
  g_radialMenu.suppressNextClick = true;
  executeRadialDirection(iconIndex, row, col, g_radialMenu.selectedDirection);
  return true;
}

void rebuildGridUI() {
  lvgl_port_lock(-1);
  ensureButtonTapStyles();

  lv_obj_t* screen = lv_scr_act();
  lv_obj_clean(screen);

  screensaverObj = nullptr;
  isScreensaverActive = false;
  g_radialMenu.active = false;
  g_radialMenu.overlay = nullptr;
  g_radialMenu.sourceButton = nullptr;
  g_radialMenu.selectedDirection = -1;
  for (int direction = 0; direction < RADIAL_DIRECTION_COUNT; direction++) {
    g_radialMenu.itemObjects[direction] = nullptr;
  }

  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);

  int total_width = GRID_COLS * BUTTON_SIZE + (GRID_COLS - 1) * BUTTON_GAP;
  int total_height = GRID_ROWS * BUTTON_SIZE + (GRID_ROWS - 1) * BUTTON_GAP;
  int start_x = (800 - total_width) / 2;
  int start_y = (480 - total_height) / 2;

  for (int row = 0; row < GRID_ROWS; row++) {
    Serial.printf("UI row %d\n", row);
    for (int col = 0; col < GRID_COLS; col++) {
      lv_obj_t* btn = lv_btn_create(screen);

      lv_obj_remove_style_all(btn);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x101010), 0);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(btn, 1, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(0x3A3A3A), 0);
      lv_obj_set_style_radius(btn, 0, 0);

      lv_obj_set_size(btn, BUTTON_SIZE, BUTTON_SIZE);
      lv_obj_set_pos(btn, start_x + col * (BUTTON_SIZE + BUTTON_GAP),
                     start_y + row * (BUTTON_SIZE + BUTTON_GAP));
      lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
      lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN |
                                 LV_OBJ_FLAG_GESTURE_BUBBLE);
      lv_obj_add_style(btn, &g_buttonTapStyleDefault, 0);
      lv_obj_add_style(btn, &g_buttonTapStylePressed, LV_STATE_PRESSED);

      uint32_t position = (row << 16) | col;
      lv_obj_set_user_data(btn, (void*)position);

      char icon_path[32];
      snprintf(icon_path, sizeof(icon_path), ICON_FORMAT, row, col);

      lv_obj_t* icon = create_icon_image(icon_path);
      if (icon) {
        lv_obj_set_parent(icon, btn);
        lv_obj_center(icon);
      } else {
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d,%d", row, col);
        lv_obj_set_style_text_color(label, lv_color_hex(0xB0B0B0), 0);
        lv_obj_center(label);
      }

      lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_ALL, nullptr);
    }
  }

  lvgl_port_unlock();
  resetActivityTimer();
}

// Generic button event handler
static void btn_event_handler(lv_event_t* e) {
  // Reset the activity timer on any button interaction
  resetActivityTimer();

  lv_obj_t* btn = lv_event_get_target(e);
  uint32_t position = (uint32_t)lv_obj_get_user_data(btn);
  int row = position >> 16;
  int col = position & 0xFFFF;
  int iconIndex = row * GRID_COLS + col;
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_PRESSED) {
    g_radialMenu.suppressNextClick = false;
    g_radialMenu.hasPressPoint = false;
    lv_area_t coords;
    lv_obj_get_coords(btn, &coords);
    g_radialMenu.origin.x = (coords.x1 + coords.x2) / 2;
    g_radialMenu.origin.y = (coords.y1 + coords.y2) / 2;

    if (getActivePointerPoint(g_radialMenu.pressPoint)) {
      g_radialMenu.hasPressPoint = true;
    } else {
      g_radialMenu.pressPoint = g_radialMenu.origin;
      g_radialMenu.hasPressPoint = true;
    }
    resetRadialGestureTracking();
    return;
  }

  if (code == LV_EVENT_LONG_PRESSED) {
    return;
  }

  if (code == LV_EVENT_PRESSING) {
    if (g_radialMenu.active) {
      updateRadialMenuFromTouch();
    } else if (iconIndex >= 0 && iconIndex < TOTAL_BUTTONS) {
      lv_point_t point;
      if (getActivePointerPoint(point)) {
        updateRadialGestureCandidate(point);
      }
      maybeOpenRadialMenuFromDrag(static_cast<uint8_t>(iconIndex), row, col, btn);
    }
    return;
  }

  if (code == LV_EVENT_RELEASED) {
    if (g_radialMenu.active) {
      updateRadialMenuFromTouch();
      executeRadialMenuSelection();
      hideRadialMenu();
    } else if (iconIndex >= 0 && iconIndex < TOTAL_BUTTONS) {
      handleFastRadialRelease(static_cast<uint8_t>(iconIndex), row, col);
    }
    return;
  }

  if (code == LV_EVENT_PRESS_LOST) {
    if (g_radialMenu.active) {
      updateRadialMenuFromTouch();
      executeRadialMenuSelection();
      hideRadialMenu();
    }
    return;
  }

  if (code == LV_EVENT_CLICKED) {
    if (g_radialMenu.suppressNextClick) {
      g_radialMenu.suppressNextClick = false;
      return;
    }

    Serial.printf("Button clicked: row=%d col=%d index=%d\n", row, col,
                  iconIndex);
    emitButtonEvent(static_cast<uint8_t>(iconIndex), row, col);
    queueMacroForIcon(static_cast<uint8_t>(iconIndex));
  }
}

// Add a touch event callback to detect any touch on the screen
void setupScreensaverTouchEvents(esp_panel::drivers::Touch* tp) {
  if (tp != nullptr) {
    // Use LVGL input device iteration
    lv_indev_t* touch_indev = lv_indev_get_next(nullptr);
    while (touch_indev) {
      if (touch_indev->driver->type == LV_INDEV_TYPE_POINTER) {
        lv_indev_enable(touch_indev, true);
        break;
      }
      touch_indev = lv_indev_get_next(touch_indev);
    }
  }
}

bool initSDCard(esp_expander::Base* expander) {
  expander->digitalWrite(SD_CS, LOW);
  delay(100);  // Stabilize

  //pinMode(SD_SS, OUTPUT);
  //digitalWrite(SD_SS, HIGH);

  SPI.setHwCs(false);
  SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_SS);

  if (!SD.begin(SD_SS, SPI, SD_SPI_FREQUENCY_HZ)) {
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    return false;
  }

  // Test write to SD card
  writeFile(SD, "/init.txt", "SD Card Initialized\n");
  return true;
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting initialization...");

  // Initialize panel and board-managed IO expander first
  Serial.println("Initializing display board...");
  auto panel = new esp_panel::board::Board();
  if (panel == nullptr) {
    haltWithError("Board allocation failed");
  }
  if (!panel->init()) {
    haltWithError("Board init failed");
  }

  auto lcd = panel->getLCD();
  if (lcd == nullptr) {
    haltWithError("Board LCD handle missing");
  }
  g_lcd = lcd;

#if LVGL_PORT_AVOID_TEAR
  // Framebuffer count for avoid-tear mode
  if (!lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM)) {
    haltWithError("LCD framebuffer count config failed");
  }

#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
  auto lcdBus = lcd->getBus();
  if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
    static_cast<esp_panel::drivers::BusRGB*>(lcdBus)
        ->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
  }
#endif
#endif

  if (!panel->begin()) {
    haltWithError("Board begin failed");
  }
  g_touch = panel->getTouch();

  auto ioExpander = panel->getIO_Expander();
  if (ioExpander == nullptr || ioExpander->getBase() == nullptr) {
    haltWithError("Board IO expander unavailable");
  }
  expander = ioExpander->getBase();

  // Configure EXIO pins used by app peripherals
  expander->pinMode(SD_CS, OUTPUT);

  if (!lvgl_port_init(g_lcd, g_touch)) {
    haltWithError("LVGL port init failed");
  }

  // Initialize SD Card
  Serial.println("Initializing SD card...");
  sdCardInitialized = initSDCard(expander);
  if (!sdCardInitialized) {
    Serial.println("SD card initialization failed!");
  } else {
    Serial.println("SD card initialized successfully!");
    Serial.println("Loading macro configuration...");
    loadMacroConfigFromSD();
  }

  // Create UI
  Serial.println("Creating UI...");
  rebuildGridUI();

  // Set up touch event handler for screensaver
  setupScreensaverTouchEvents(g_touch);

  Serial.println("Setup complete!");
}

void loop() {
  if (!usbInitAttempted && millis() > 1500) {
    usbInitAttempted = true;
    initUSBKeyboard();
  }

  processCdcInput();
  tickMacroExecutor();

  // Check if it's time to activate the screensaver
  unsigned long currentTime = millis();
  if (!isScreensaverActive) {
    if (currentTime - lastActivityTime > SCREENSAVER_TIMEOUT_MS) {
      activateScreensaver();
    }
  }

  delay(g_cdcUpload.active ? 1 : 5);
}
