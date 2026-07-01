#include <Arduino.h>
#include <Adafruit_NeoTrellis.h>
#include <SPI.h>
#include "wifi_services.h"

#define SPEAKER_PIN 2
#define INT_PIN 3
// SDA 4, SCL 5
#define NUM_KEYS 16
#define NUM_ROWS 4
#define NUM_COLS 4
#define NUM_DRAW_COLORS 7
#define NUM_PUZZLE_COLORS 4
#define OFF 0

// these are mapped to LED indices
enum ModeSelect
{
  Wifi = 0,
  BrightnessUp = 1,
  BrightnessDn = 2,
  Puzzle = 14,
  Draw = 15,
};

enum GameMode
{
  DrawMode,
  PuzzleMode,
  None,
};

Adafruit_NeoTrellis neoTrellis = Adafruit_NeoTrellis();
int buttonStates[NUM_KEYS];
uint8_t brightness = 20;
GameMode gameMode = GameMode::None;
uint32_t *gameModeColors;
uint32_t gameModeNumColors;
WifiServices wifiServices;

uint32_t Off = neoTrellis.pixels.Color(0, 0, 0);
uint32_t Red = neoTrellis.pixels.Color(0xFF, 0, 0);
uint32_t Orange = neoTrellis.pixels.Color(0xFF, 0x7F, 0);
uint32_t Yellow = neoTrellis.pixels.Color(0xFF, 0xFF, 0);
// uint32_t YellowGreen = neoTrellis.pixels.Color(0x7F, 0xFF, 0);
uint32_t Green = neoTrellis.pixels.Color(0, 0xFF, 0);
// uint32_t GreenBlue = neoTrellis.pixels.Color(0, 0xFF, 0x7F);
uint32_t Blue = neoTrellis.pixels.Color(0, 0, 0xFF);
uint32_t Indigo = neoTrellis.pixels.Color(0x7F, 0, 0xFF);
uint32_t Violet = neoTrellis.pixels.Color(0xFF, 0, 0xFF);
uint32_t DrawColors[NUM_DRAW_COLORS] = {Off, Red, Orange, Yellow, Green, Blue, Indigo};
uint32_t PuzzleColors[NUM_PUZZLE_COLORS] = {Off, Blue, Green, Red};

inline int coordToIndex(int row, int col)
{
  return row * NUM_COLS + col;
}

inline int indexToRow(int index)
{
  return index / NUM_COLS;
}

inline int indexToCol(int index)
{
  return index % NUM_COLS;
}

void increment_key(int row, int col, int inc)
{
  if (row >= 0 && row < NUM_ROWS && col >= 0 && col < NUM_COLS)
  {
    int buttonIndex = coordToIndex(row, col);
    buttonStates[buttonIndex] = (buttonStates[buttonIndex] + inc) % gameModeNumColors;
    neoTrellis.pixels.setPixelColor(buttonIndex, gameModeColors[buttonStates[buttonIndex]]);
    log_i("Button %d state: %d", buttonIndex, buttonStates[buttonIndex]);
  }
}

TrellisCallback modeSelectButtonCallback(keyEvent event)
{
  if (event.bit.EDGE == SEESAW_KEYPAD_EDGE_RISING)
  {
    int delta = 0;

    switch (event.bit.NUM)
    {
    case ModeSelect::Draw:
      gameMode = GameMode::DrawMode;
      gameModeColors = DrawColors;
      gameModeNumColors = NUM_DRAW_COLORS;
      log_i("Game Mode: draw");
      break;
    case ModeSelect::Puzzle:
      gameMode = GameMode::PuzzleMode;
      gameModeColors = PuzzleColors;
      gameModeNumColors = NUM_PUZZLE_COLORS;
      log_i("Game Mode: puzzle");
      break;
    case ModeSelect::BrightnessUp:
    case ModeSelect::BrightnessDn:
      delta = event.bit.NUM == ModeSelect::BrightnessUp ? 17 : -17;
      brightness = constrain(brightness + delta, 5, 100);
      neoTrellis.pixels.setBrightness(brightness);

      // TODO: factor into helper shared with setup
      neoTrellis.pixels.setPixelColor(ModeSelect::Draw, Red);
      neoTrellis.pixels.setPixelColor(ModeSelect::Puzzle, Green);
      neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessUp, Yellow);
      neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessDn, Orange);
      if (neoTrellis.pixels.getPixelColor(ModeSelect::Wifi) != Off)
      {
        neoTrellis.pixels.setPixelColor(ModeSelect::Wifi, Blue);
      }
      neoTrellis.pixels.show();
      log_i("Brightness: %d", brightness);
      break;
    case ModeSelect::Wifi:
      wifiServices.setup(DEVICE_NAME);
      break;
    }
  }

  return NULL;
}

TrellisCallback gameButtonCallback(keyEvent event)
{
  if (event.bit.EDGE == SEESAW_KEYPAD_EDGE_RISING)
  {
    int key = event.bit.NUM;
    int row = indexToRow(key);
    int col = indexToCol(key);
    log_i("%d,%d pressed", row, col);

    switch (gameMode)
    {
    case GameMode::DrawMode:
      increment_key(row, col, 1);
      break;
    case GameMode::PuzzleMode:
      increment_key(row, col, 2);
      increment_key(row - 1, col, 1);
      increment_key(row + 1, col, 1);
      increment_key(row, col - 1, 1);
      increment_key(row, col + 1, 1);
      break;
    default:
      break;
    }

    neoTrellis.pixels.show();
    tone(SPEAKER_PIN, 1000 + key * 100);
  }
  else if (event.bit.EDGE == SEESAW_KEYPAD_EDGE_FALLING)
  {
    noTone(SPEAKER_PIN);
  }

  return NULL;
}

void setup()
{
  Serial.begin(115200);
  log_i("Starting setup...");

  pinMode(INT_PIN, INPUT);

  neoTrellis.begin();
  neoTrellis.pixels.setBrightness(brightness);

  // setup mode select callbacks
  neoTrellis.activateKey(ModeSelect::Draw, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::Puzzle, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::BrightnessUp, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::BrightnessDn, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::Wifi, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.registerCallback(ModeSelect::Draw, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::Puzzle, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::BrightnessUp, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::BrightnessDn, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::Wifi, modeSelectButtonCallback);
  neoTrellis.pixels.setPixelColor(ModeSelect::Draw, Red);
  neoTrellis.pixels.setPixelColor(ModeSelect::Puzzle, Green);
  neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessUp, Yellow);
  neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessDn, Orange);
  neoTrellis.pixels.setPixelColor(ModeSelect::Wifi, Blue);

  neoTrellis.pixels.show();

  unsigned long lastToggleMillis = millis();
  unsigned long toggleDelayMs = 1000;
  bool connectionAttempted = false;
  bool isBlue = true;
  while (true)
  {
    neoTrellis.read();

    // break once game mode is selected
    if (gameMode != GameMode::None)
    {
      break;
    }

    // wifi led blinks with alt color status: off=connecting, green=connected, red=connection failed
    if (millis() - lastToggleMillis > toggleDelayMs)
    {
      connectionAttempted |= !connectionAttempted && wifiServices.isConnecting();
      
      uint32_t altColor = connectionAttempted ? Red : Blue;
      altColor = wifiServices.isConnected() ? Green : altColor;
      altColor = wifiServices.isConnecting() ? Off : altColor;

      isBlue = !isBlue;
      neoTrellis.pixels.setPixelColor(ModeSelect::Wifi, isBlue ? Blue : altColor);
      neoTrellis.pixels.show();
      
      lastToggleMillis = millis();
      toggleDelayMs = isBlue ? 1000 : 100;
    }
    delay(20);
  }

  // unregister callbacks for setup
  neoTrellis.unregisterCallback(ModeSelect::Draw);
  neoTrellis.unregisterCallback(ModeSelect::Puzzle);
  neoTrellis.unregisterCallback(ModeSelect::BrightnessUp);
  neoTrellis.unregisterCallback(ModeSelect::BrightnessDn);
  neoTrellis.unregisterCallback(ModeSelect::Wifi);

  // register callbacks for game mode
  for (int i = 0; i < NUM_KEYS; i++)
  {
    // ok to activate twice (less code)
    neoTrellis.activateKey(i, SEESAW_KEYPAD_EDGE_RISING);
    neoTrellis.activateKey(i, SEESAW_KEYPAD_EDGE_FALLING);
    neoTrellis.registerCallback(i, gameButtonCallback);
  }

  // random initial state
  randomSeed(analogRead(0));
  for (int i = 0; i < NUM_KEYS; i++)
  {
    buttonStates[i] = random(gameModeNumColors);
    neoTrellis.pixels.setPixelColor(i, gameModeColors[buttonStates[i]]);
  }
  neoTrellis.pixels.show();

  log_i("Setup complete");
}

void loop()
{
  if (!digitalRead(INT_PIN))
  {
    neoTrellis.read(false);
  }
  // neoTrellis.read();
  delay(20);
}
