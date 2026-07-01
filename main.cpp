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
  Snake = 13,
  Puzzle = 14,
  Draw = 15,
};

enum GameMode
{
  DrawMode,
  PuzzleMode,
  SnakeMode,
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

#define SNAKE_MAX_LEN 6
#define SNAKE_MOVE_INTERVAL_MS 800
#define OBSTACLE_SPAWN_INTERVAL_MS 3000
#define MAX_OBSTACLES 10

int snakeBody[SNAKE_MAX_LEN]; // [0] = head
int snakeLen = 1;
int obstacles[MAX_OBSTACLES];
int numObstacles = 0;
int foodCell = -1; // -1 = no food currently on board
bool snakeGameOver = false;

unsigned long lastSnakeMoveMillis = 0;
unsigned long lastSnakeGrowMillis = 0;
unsigned long lastObstacleSpawnMillis = 0;

uint32_t SnakeHeadColor = Violet;
uint32_t SnakeBodyColor = Indigo;
uint32_t SnakeDeadColor = Blue;
uint32_t ObstacleColor = Red;
uint32_t FoodColor = Yellow;

inline int coordToIndex(int row, int col)
{
  return row * NUM_COLS + col;
}

// precomputed once, indexed 0..NUM_KEYS-1
const uint8_t rowLookup[NUM_KEYS] = {
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3};

const uint8_t colLookup[NUM_KEYS] = {
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3};

inline int indexToRow(int index)
{
  // index / NUM_COLS
  return rowLookup[index];
}

inline int indexToCol(int index)
{
  // index % NUM_COLS
  return colLookup[index];
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

int getAdjacentCells(int fromindex, int outCells[4])
{
  int row = indexToRow(fromindex), col = indexToCol(fromindex);
  int dr[] = {-1, 1, 0, 0}, dc[] = {0, 0, -1, 1};
  int n = 0;
  for (int d = 0; d < 4; d++)
  {
    int r = row + dr[d], c = col + dc[d];
    if (r >= 0 && r < NUM_ROWS && c >= 0 && c < NUM_COLS)
    {
      outCells[n++] = coordToIndex(r, c);
    }
  }
  return n; // count of valid neighbors written into outCells
}

bool isFoodCell(int index)
{
  return foodCell != -1 && foodCell == index;
}

bool isObstacleCell(int index)
{
  for (int i = 0; i < numObstacles; i++)
  {
    if (obstacles[i] == index)
    {
      return true;
    }
  }

  return false;
}

bool isSnakeCell(int index)
{
  for (int i = 0; i < snakeLen; i++)
  {
    if (snakeBody[i] == index)
    {
      return true;
    }
  }

  return false;
}

bool isAdjacentToHead(int index)
{
  int headRow = indexToRow(snakeBody[0]);
  int headCol = indexToCol(snakeBody[0]);
  int row = indexToRow(index);
  int col = indexToCol(index);
  int rowDist = abs(row - headRow);
  int colDist = abs(col - headCol);
  return (rowDist + colDist) <= 1; // includes head cell itself (dist 0) and 4 orthogonal neighbors
}

void spawnFood()
{
  if (foodCell != -1)
  {
    return;
  }

  int index, attempts = 0;
  do
  {
    index = random(NUM_KEYS);
    attempts++;
  } while ((isSnakeCell(index) || isObstacleCell(index)) && attempts < 20);

  if (!isSnakeCell(index) && !isObstacleCell(index))
  {
    foodCell = index;
  }
}

void removeFoodAt(int index)
{
  if (foodCell == index)
  {
    foodCell = -1;
  }
}

void spawnObstacle()
{
  if (numObstacles >= MAX_OBSTACLES)
  {
    return;
  }

  int index, attempts = 0;
  do
  {
    index = random(NUM_KEYS);
    attempts++;
  } while ((isSnakeCell(index) || isObstacleCell(index) || isFoodCell(index) || isAdjacentToHead(index)) && attempts < 20);

  if (!isSnakeCell(index) && !isObstacleCell(index) && !isFoodCell(index) && !isAdjacentToHead(index))
  {
    obstacles[numObstacles++] = index;
  }
}

void removeObstacleAt(int index)
{
  for (int i = 0; i < numObstacles; i++)
  {
    if (obstacles[i] == index)
    {
      obstacles[i] = obstacles[--numObstacles]; // swap-remove
      return;
    }
  }
}

int getSnakeNextCell()
{
  int neighbors[4];
  int n = getAdjacentCells(snakeBody[0], neighbors);

  int legalMoves[4];
  int numValid = 0;
  for (int i = 0; i < n; i++)
  {
    if (!isSnakeCell(neighbors[i]))
    {
      legalMoves[numValid++] = neighbors[i];
    }
  }

  if (numValid == 0)
  {
    return -1; // trapped
  }

  return legalMoves[random(numValid)];
}

void moveSnake()
{
  int nextCell = getSnakeNextCell();

  // trapped or dead
  if (nextCell == -1 || isObstacleCell(nextCell))
  {
    snakeGameOver = true;
    return;
  }

  bool ateFood = isFoodCell(nextCell);

  // shift body toward head
  for (int i = snakeLen - 1; i > 0; i--)
  {
    snakeBody[i] = snakeBody[i - 1];
  }
  snakeBody[0] = nextCell;

  if (ateFood)
  {
    foodCell = -1; // consumed
    if (snakeLen < SNAKE_MAX_LEN)
    {
      snakeLen++; // tail segment implicitly extends since snakeBody[snakeLen-1] retains old tail position
    }
  }
}

void shrinkSnake()
{
  if (snakeLen > 1)
  {
    snakeLen--;
  }
}

void drawSnakeGrid()
{
  for (int i = 0; i < NUM_KEYS; i++)
  {
    neoTrellis.pixels.setPixelColor(i, Off);
  }

  if (foodCell != -1)
  {
    neoTrellis.pixels.setPixelColor(foodCell, FoodColor);
  }

  for (int i = 0; i < numObstacles; i++)
  {
    neoTrellis.pixels.setPixelColor(obstacles[i], ObstacleColor);
  }

  for (int i = 0; i < snakeLen; i++)
  {
    uint32_t color = snakeGameOver ? SnakeDeadColor : (i == 0 ? SnakeHeadColor : SnakeBodyColor);
    neoTrellis.pixels.setPixelColor(snakeBody[i], color);
  }

  neoTrellis.pixels.show();
}

void snakeGameTick()
{
  if (snakeGameOver)
  {
    return;
  }

  if (millis() - lastObstacleSpawnMillis > OBSTACLE_SPAWN_INTERVAL_MS)
  {
    spawnObstacle();
    lastObstacleSpawnMillis = millis();
  }

  spawnFood();
  if (millis() - lastSnakeMoveMillis > SNAKE_MOVE_INTERVAL_MS)
  {
    moveSnake();
    lastSnakeMoveMillis = millis();
  }
  drawSnakeGrid();
}

void resetSnakeGame()
{
  snakeLen = 1;
  // snake head stays where it's at
  numObstacles = 0;
  foodCell = -1;
  snakeGameOver = false;
  lastSnakeMoveMillis = millis();
  lastObstacleSpawnMillis = millis();
}

void initRandomColorState()
{
  // random initial state
  randomSeed(analogRead(0));
  for (int i = 0; i < NUM_KEYS; i++)
  {
    buttonStates[i] = random(gameModeNumColors);
    neoTrellis.pixels.setPixelColor(i, gameModeColors[buttonStates[i]]);
  }
  neoTrellis.pixels.show();
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
      initRandomColorState();
      log_i("Game Mode: draw");
      break;
    case ModeSelect::Puzzle:
      gameMode = GameMode::PuzzleMode;
      gameModeColors = PuzzleColors;
      gameModeNumColors = NUM_PUZZLE_COLORS;
      initRandomColorState();
      log_i("Game Mode: puzzle");
      break;
    case ModeSelect::Snake:
      gameMode = GameMode::SnakeMode;
      snakeBody[0] = random(NUM_KEYS);
      log_i("Game Mode: snake");
      break;
    case ModeSelect::BrightnessUp:
    case ModeSelect::BrightnessDn:
      delta = event.bit.NUM == ModeSelect::BrightnessUp ? 17 : -17;
      brightness = constrain(brightness + delta, 5, 100);
      neoTrellis.pixels.setBrightness(brightness);

      // TODO: factor into helper shared with setup
      neoTrellis.pixels.setPixelColor(ModeSelect::Draw, Red);
      neoTrellis.pixels.setPixelColor(ModeSelect::Puzzle, Green);
      neoTrellis.pixels.setPixelColor(ModeSelect::Snake, Violet);
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
      tone(SPEAKER_PIN, 1000 + key * 100);
      break;
    case GameMode::PuzzleMode:
      increment_key(row, col, 2);
      increment_key(row - 1, col, 1);
      increment_key(row + 1, col, 1);
      increment_key(row, col - 1, 1);
      increment_key(row, col + 1, 1);
      tone(SPEAKER_PIN, 1000 + key * 100);
      break;
    case GameMode::SnakeMode:
      if (snakeGameOver)
      {
        if (isSnakeCell(key))
        {
          resetSnakeGame();
          tone(SPEAKER_PIN, 1500);
        }
        else
        {
          tone(SPEAKER_PIN, 400);
        }
      }
      else if (isFoodCell(key))
      {
        removeFoodAt(key);
        tone(SPEAKER_PIN, 600);
      }
      else if (isObstacleCell(key))
      {
        removeObstacleAt(key);
        tone(SPEAKER_PIN, 1500);
      }
      else if (isSnakeCell(key))
      {
        shrinkSnake();
        tone(SPEAKER_PIN, 400);
      }
      drawSnakeGrid();
      break;
    default:
      break;
    }

    neoTrellis.pixels.show();
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
  neoTrellis.activateKey(ModeSelect::Snake, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::BrightnessUp, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::BrightnessDn, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::Wifi, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.registerCallback(ModeSelect::Draw, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::Puzzle, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::Snake, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::BrightnessUp, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::BrightnessDn, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::Wifi, modeSelectButtonCallback);
  neoTrellis.pixels.setPixelColor(ModeSelect::Draw, Red);
  neoTrellis.pixels.setPixelColor(ModeSelect::Puzzle, Green);
  neoTrellis.pixels.setPixelColor(ModeSelect::Snake, Violet);
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
  neoTrellis.unregisterCallback(ModeSelect::Snake);
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

  log_i("Setup complete");
}

void loop()
{
  if (!digitalRead(INT_PIN))
  {
    neoTrellis.read(false);
  }

  if (gameMode == GameMode::SnakeMode)
  {
    snakeGameTick();
  }

  delay(20);
}
