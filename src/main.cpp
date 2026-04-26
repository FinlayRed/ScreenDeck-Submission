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
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cstdarg>
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
#define MACRO_CONFIG_VERSION 1
#define MAX_ACTIONS_PER_ICON 24
#define FALLBACK_ICON_PATH "/fallback.bin"
#define ICON_FORMAT "/icon_%d_%d.bin"  // Format: icon_row_col.bin

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

struct IconMacro {
  MacroAction actions[MAX_ACTIONS_PER_ICON];
  uint8_t actionCount = 0;
};

struct MacroExecutorState {
  bool active = false;
  bool comboPressed = false;
  uint8_t iconIndex = 0;
  uint8_t actionIndex = 0;
  unsigned long nextActionAtMs = 0;
};

struct CdcUploadSession {
  bool active = false;
  char targetPath[64] = {0};
  char tempPath[72] = {0};
  size_t expectedBytes = 0;
  size_t receivedBytes = 0;
  unsigned long lastDataMs = 0;
  uint8_t sourcePort = 0;
  File file;
};

enum class CdcPortId : uint8_t {
  SerialPort = 0,
  UsbCdcPort = 1,
};

IconMacro g_iconMacros[TOTAL_BUTTONS];
MacroExecutorState g_macroExecutor;
CdcUploadSession g_cdcUpload;

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
constexpr unsigned long kCdcUploadTimeoutMs = 30000;

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
  char message[192];

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
  char message[192];

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

static bool isAllowedUploadPath(const char* path) {
  if (path == nullptr) {
    return false;
  }

  if (strcmp(path, MACRO_CONFIG_PATH) == 0 ||
      strcmp(path, FALLBACK_ICON_PATH) == 0) {
    return true;
  }

  int row = -1;
  int col = -1;
  return parseIconPath(path, row, col);
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
  return parseIconPath(path, row, col);
}

static void clearMacroConfig() {
  for (int i = 0; i < TOTAL_BUTTONS; i++) {
    g_iconMacros[i].actionCount = 0;
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
  file.println("  \"version\": 1,");
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
  if (version != MACRO_CONFIG_VERSION) {
    Serial.printf("macros.json version mismatch (got %d, expected %d)\n", version,
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

    JsonArrayConst actions = icon["actions"].as<JsonArrayConst>();
    if (actions.isNull()) {
      continue;
    }

    for (JsonVariantConst actionValue : actions) {
      JsonObjectConst actionObj = actionValue.as<JsonObjectConst>();
      if (actionObj.isNull()) {
        continue;
      }

      if (g_iconMacros[index].actionCount >= MAX_ACTIONS_PER_ICON) {
        Serial.printf("Icon %d has too many actions (max %d)\n", index, MAX_ACTIONS_PER_ICON);
        break;
      }

      const char* actionType = actionObj["type"] | "";
      MacroAction action;

      if (equalsIgnoreCase(actionType, "combo")) {
        uint8_t keycode = 0;
        const char* keyName = actionObj["key"] | "";
        if (!keyNameToKeycode(keyName, keycode)) {
          Serial.printf("Invalid key '%s' on icon %d, action skipped\n", keyName, index);
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

      g_iconMacros[index].actions[g_iconMacros[index].actionCount++] = action;
    }
  }

  int configuredIcons = 0;
  int totalActions = 0;
  int missingIcons = 0;
  for (int i = 0; i < TOTAL_BUTTONS; i++) {
    if (g_iconMacros[i].actionCount > 0) {
      configuredIcons++;
      totalActions += g_iconMacros[i].actionCount;
    }
    if (!seenIndexes[i]) {
      missingIcons++;
    }
  }

  Serial.printf("Loaded macros.json: %d icons with actions, %d total actions\n", configuredIcons,
                totalActions);
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

static void queueMacroForIcon(uint8_t iconIndex) {
  if (iconIndex >= TOTAL_BUTTONS) {
    return;
  }

  if (!usbKeyboardReady) {
    Serial.println("USB not ready, macro ignored");
    return;
  }

  if (g_iconMacros[iconIndex].actionCount == 0) {
    Serial.printf("Icon %u has no macro actions\n", iconIndex);
    return;
  }

  if (g_macroExecutor.active) {
    keyboard.releaseAll();
  }

  g_macroExecutor.active = true;
  g_macroExecutor.comboPressed = false;
  g_macroExecutor.iconIndex = iconIndex;
  g_macroExecutor.actionIndex = 0;
  g_macroExecutor.nextActionAtMs = millis();

  Serial.printf("Queued macro for icon %u (%u actions)\n", iconIndex,
                g_iconMacros[iconIndex].actionCount);
}

static void tickMacroExecutor() {
  if (!usbKeyboardReady || !g_macroExecutor.active) {
    return;
  }

  unsigned long now = millis();
  if (now < g_macroExecutor.nextActionAtMs) {
    return;
  }

  IconMacro& macro = g_iconMacros[g_macroExecutor.iconIndex];
  if (g_macroExecutor.actionIndex >= macro.actionCount) {
    keyboard.releaseAll();
    g_macroExecutor.active = false;
    g_macroExecutor.comboPressed = false;
    return;
  }

  const MacroAction& action = macro.actions[g_macroExecutor.actionIndex];
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
  g_cdcUpload.targetPath[0] = '\0';
  g_cdcUpload.tempPath[0] = '\0';
  g_cdcUpload.expectedBytes = 0;
  g_cdcUpload.receivedBytes = 0;
  g_cdcUpload.lastDataMs = 0;
  g_cdcUpload.sourcePort = static_cast<uint8_t>(CdcPortId::SerialPort);
}

static CdcPortId activeUploadPortId() {
  return static_cast<CdcPortId>(g_cdcUpload.sourcePort);
}

static void abortCdcUpload(const char* reason) {
  if (g_cdcUpload.file) {
    g_cdcUpload.file.close();
  }

  if (g_cdcUpload.tempPath[0] != '\0' && SD.exists(g_cdcUpload.tempPath)) {
    SD.remove(g_cdcUpload.tempPath);
  }

  cdcReplyToPort(activeUploadPortId(), "CDC:ERR PUT %s", reason);
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

  if (strcmp(g_cdcUpload.targetPath, MACRO_CONFIG_PATH) == 0) {
    loadMacroConfigFromSD();
  }

  cdcReplyToPort(sourcePort, "CDC:OK PUT %s %u", g_cdcUpload.targetPath,
                 static_cast<unsigned int>(g_cdcUpload.receivedBytes));

  if (strcmp(g_cdcUpload.targetPath, MACRO_CONFIG_PATH) == 0) {
    cdcReplyToPort(sourcePort, "CDC:INFO RELOAD MACROS");
  } else if (isIconAssetPath(g_cdcUpload.targetPath)) {
    cdcReplyToPort(sourcePort, "CDC:INFO ICON STORED");
  }

  resetCdcUploadState();
}

static void beginCdcUpload(CdcPortId sourcePort, const char* targetPath,
                           size_t expectedBytes) {
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

  if (expectedBytes == 0 || expectedBytes > 1024 * 1024) {
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

  g_cdcUpload.file = SD.open(g_cdcUpload.tempPath, FILE_WRITE);
  if (!g_cdcUpload.file) {
    resetCdcUploadState();
    cdcReplyToPort(sourcePort, "CDC:ERR OPEN_TEMP_FAILED");
    return;
  }

  g_cdcUpload.active = true;
  g_cdcUpload.receivedBytes = 0;
  g_cdcUpload.lastDataMs = millis();
  g_cdcUpload.sourcePort = static_cast<uint8_t>(sourcePort);

  cdcReplyToPort(sourcePort, "CDC:READY PUT %s %u", g_cdcUpload.targetPath,
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
  if (fileSize > 1024 * 1024) {
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
                   "CDC:CMDS PING|STATUS|RELOAD <MACROS|ICONS|ALL>|PUT <path> <size>|GET <path>");
    return;
  }

  if (equalsIgnoreCase(line, "STATUS")) {
    int configuredIcons = 0;
    for (int i = 0; i < TOTAL_BUTTONS; i++) {
      if (g_iconMacros[i].actionCount > 0) {
        configuredIcons++;
      }
    }

    cdcReplyToPort(sourcePort, "CDC:STATUS sd=%d usb=%d macros=%d active=%d events=1 proto=2",
                   sdCardInitialized ? 1 : 0, usbKeyboardReady ? 1 : 0,
                   configuredIcons, g_macroExecutor.active ? 1 : 0);
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

    beginCdcUpload(sourcePort, path, static_cast<size_t>(uploadSize));
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

static void processCdcInputForPort(CdcPortId portId, Stream* inputStream,
                                   char* lineBuffer,
                                   size_t* lineLength) {
  while (inputStream->available() > 0) {
    if (uploadBelongsToPort(portId)) {
      uint8_t chunk[256];
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

void rebuildGridUI() {
  lvgl_port_lock(-1);
  ensureButtonTapStyles();

  lv_obj_t* screen = lv_scr_act();
  lv_obj_clean(screen);

  screensaverObj = nullptr;
  isScreensaverActive = false;

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
      lv_obj_add_style(btn, &g_buttonTapStyleDefault, 0);
      lv_obj_add_style(btn, &g_buttonTapStylePressed, LV_STATE_PRESSED);

      uint32_t position = (row << 16) | col;
      lv_obj_set_user_data(btn, (void*)position);

      char icon_path[32];
      snprintf(icon_path, sizeof(icon_path), ICON_FORMAT, row, col);

      lv_obj_t* icon = create_icon_image(icon_path);
      if (icon) {
        lv_obj_center(icon);
        lv_obj_set_parent(icon, btn);
      } else {
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "%d,%d", row, col);
        lv_obj_set_style_text_color(label, lv_color_hex(0xB0B0B0), 0);
        lv_obj_center(label);
      }

      lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_CLICKED, nullptr);
    }
  }

  lvgl_port_unlock();
  resetActivityTimer();
}

// Generic button event handler
static void btn_event_handler(lv_event_t* e) {
  // Reset the activity timer on any button interaction
  resetActivityTimer();

  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t* btn = lv_event_get_target(e);
    uint32_t position = (uint32_t)lv_obj_get_user_data(btn);
    int row = position >> 16;
    int col = position & 0xFFFF;
    int iconIndex = row * GRID_COLS + col;

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

  if (!SD.begin(SD_SS)) {
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

  auto ioExpander = panel->getIO_Expander();
  if (ioExpander == nullptr || ioExpander->getBase() == nullptr) {
    haltWithError("Board IO expander unavailable");
  }
  expander = ioExpander->getBase();

  // Configure EXIO pins used by app peripherals
  expander->pinMode(SD_CS, OUTPUT);

  if (!lvgl_port_init(panel->getLCD(), panel->getTouch())) {
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
  setupScreensaverTouchEvents(panel->getTouch());

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
