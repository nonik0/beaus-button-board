#include <Arduino.h>
#include <Adafruit_NeoTrellis.h>
#include <SPI.h>
#include "wifi_services.h"

#define SPEAKER_PIN 2
#define NUM_KEYS 16
#define NUM_ROWS 4
#define NUM_COLS 4
#define NUM_DRAW_COLORS 7
#define NUM_PUZZLE_COLORS 4
#define OFF 0

enum GameMode
{
  Draw = 15,
  Puzzle = 14,
  None = 0,
};

Adafruit_NeoTrellis neoTrellis = Adafruit_NeoTrellis();
int buttonStates[NUM_KEYS];
GameMode gameMode = GameMode::None;
uint32_t *gameModeColors;
uint32_t gameModeNumColors;
WifiServices wifiServices;

uint32_t Off = neoTrellis.pixels.Color(0, 0, 0);
uint32_t Red = neoTrellis.pixels.Color(0xFF, 0, 0);
uint32_t Orange = neoTrellis.pixels.Color(0xFF, 0x7F, 0);
uint32_t Yellow = neoTrellis.pixels.Color(0xFF, 0xFF, 0);
//uint32_t YellowGreen = neoTrellis.pixels.Color(0x7F, 0xFF, 0);
uint32_t Green = neoTrellis.pixels.Color(0, 0xFF, 0);
//uint32_t GreenBlue = neoTrellis.pixels.Color(0, 0xFF, 0x7F);
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

TrellisCallback gameButtonCallback(keyEvent evt)
{
  if (evt.bit.EDGE == SEESAW_KEYPAD_EDGE_RISING)
  {
    int key = evt.bit.NUM;
    int row = indexToRow(key);
    int col = indexToCol(key);
    log_i("%d,%d pressed", row, col);

    switch (gameMode)
    {
    case GameMode::Draw:
      increment_key(row, col, 1);
      break;
    case GameMode::Puzzle:
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
  else if (evt.bit.EDGE == SEESAW_KEYPAD_EDGE_FALLING)
  {
    noTone(SPEAKER_PIN);
  }

  return NULL;
}

TrellisCallback gameModeSelectCallback(keyEvent evt)
{
  if (evt.bit.EDGE == SEESAW_KEYPAD_EDGE_RISING)
  {
    switch (evt.bit.NUM)
    {
    case GameMode::Draw:
      gameMode = GameMode::Draw;
      gameModeColors = DrawColors;
      gameModeNumColors = NUM_DRAW_COLORS;
      break;
    case GameMode::Puzzle:
      log_i("Game mode: 1");
      gameMode = GameMode::Puzzle;
      gameModeColors = PuzzleColors;
      gameModeNumColors = NUM_PUZZLE_COLORS;
      break;
    default:
      break;
    }
  }

  return NULL;
}

TrellisCallback wifiSetupCallback(keyEvent evt)
{
  if (evt.bit.EDGE == SEESAW_KEYPAD_EDGE_RISING && evt.bit.NUM == 0)
  {
    log_i("Button 0 is pressed, setting up wifi services");
    neoTrellis.pixels.setPixelColor(0, Blue);
    wifiServices.setup(DEVICE_NAME);
    wifiServices.createTask();

    // wait to connect, toggling color while waiting
    const unsigned long ConnectTimeoutMs = 10000;
    unsigned long startMs = millis();
    unsigned long lastToggleMillis = millis();
    bool isBlue = true;
    while (!wifiServices.isConnected() && millis() - startMs < ConnectTimeoutMs)
    {
      if (millis() - lastToggleMillis > 100)
      {
        lastToggleMillis = millis();
        neoTrellis.pixels.setPixelColor(0, isBlue ? Green : Blue);
        neoTrellis.pixels.show();
        isBlue = !isBlue;
      }
    }

    // indicate connection status
    neoTrellis.pixels.setPixelColor(0, wifiServices.isConnected() ? Green : Red);
    neoTrellis.pixels.show();
    delay(2000);
  }

  return NULL;
}

void setup()
{
  Serial.begin(115200);
  log_i("Starting setup...");

  neoTrellis.begin();
  neoTrellis.pixels.setBrightness(50);

  // setup wifi/ota button callback
  neoTrellis.activateKey(0, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.registerCallback(0, wifiSetupCallback);

  // setup game mode select callback
  neoTrellis.activateKey(GameMode::Draw, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(GameMode::Puzzle, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.registerCallback(GameMode::Draw, gameModeSelectCallback);
  neoTrellis.registerCallback(GameMode::Puzzle, gameModeSelectCallback);
  neoTrellis.pixels.setPixelColor(GameMode::Draw, Red);
  neoTrellis.pixels.setPixelColor(GameMode::Puzzle, Green);

  neoTrellis.pixels.show();

  // wait for input to select game mode or wifi setup
  const unsigned long ConnectPressTimeoutMs = 5000;
  unsigned long startMs = millis();
  unsigned long lastToggleMillis = millis();
  bool isOn = true;

  while (millis() - startMs < ConnectPressTimeoutMs)
  {
    neoTrellis.read();

    // break when wifi is connected or game mode is selected
    if (wifiServices.isConnected() || gameMode != GameMode::None)
    {
      break;
    }
    // toggle wifi button
    else if (millis() - lastToggleMillis > 200)
    {
      lastToggleMillis = millis();
      neoTrellis.pixels.setPixelColor(0, isOn ? Blue : Off);
      neoTrellis.pixels.show();
      isOn = !isOn;
    }
    delay(20);
  }

  // unregister callbacks for setup
  neoTrellis.unregisterCallback(0);
  neoTrellis.unregisterCallback(GameMode::Draw);
  neoTrellis.unregisterCallback(GameMode::Puzzle);

  // register callbacks for game mode
  for (int i = 0; i < NUM_KEYS; i++)
  {
    // ok to activate twice (less code)
    neoTrellis.activateKey(i, SEESAW_KEYPAD_EDGE_RISING);
    neoTrellis.activateKey(i, SEESAW_KEYPAD_EDGE_FALLING);
    neoTrellis.registerCallback(i, gameButtonCallback);
  }

  // default if no game mode is selected
  if (gameMode == GameMode::None)
  {
    gameMode = GameMode::Draw;
    gameModeColors = DrawColors;
    gameModeNumColors = NUM_DRAW_COLORS;
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
  neoTrellis.read();
  delay(20);
}
