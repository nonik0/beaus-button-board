#include <Arduino.h>
#include <Adafruit_NeoTrellis.h>
#include <SPI.h>

#include "snake.h"
#include "snakeBehavior.h"
#include "wifi_services.h"

#define SPEAKER_PIN 2
#define INT_PIN 3
// SDA 4, SCL 5

#define NUM_DRAW_COLORS 7
#define NUM_PUZZLE_COLORS 4
#define OFF 0

// these are mapped to LED indices
enum ModeSelect
{
  WifiInit = 0,
  BrightnessUp = 2,
  BrightnessDn = 3,
  Snake2Select = 12,
  SnakeSelect = 13,
  PuzzleSelect = 14,
  DrawSelect = 15,
};

enum GameMode
{
  DrawMode,
  PuzzleMode,
  SnakeMode,
  Snake2Mode,
  None,
};

Adafruit_NeoTrellis neoTrellis = Adafruit_NeoTrellis();
int buttonStates[NUM_KEYS];
int brightness = 25;
GameMode gameMode = GameMode::None;
uint32_t *gameModeColors;
uint32_t gameModeNumColors;
WifiServices wifiServices;

uint32_t Off = neoTrellis.pixels.Color(0, 0, 0);
uint32_t Red = neoTrellis.pixels.Color(0xFF, 0, 0);
uint32_t Orange = neoTrellis.pixels.Color(0xFF, 0x7F, 0);
uint32_t Yellow = neoTrellis.pixels.Color(0xFF, 0xFF, 0);
uint32_t YellowGreen = neoTrellis.pixels.Color(0x7F, 0xFF, 0);
uint32_t Green = neoTrellis.pixels.Color(0, 0xFF, 0);
uint32_t GreenBlue = neoTrellis.pixels.Color(0, 0xFF, 0x7F);
uint32_t Blue = neoTrellis.pixels.Color(0, 0, 0xFF);
uint32_t Indigo = neoTrellis.pixels.Color(0x7F, 0, 0xFF);
uint32_t Violet = neoTrellis.pixels.Color(0xFF, 0, 0xFF);
uint32_t DrawColors[NUM_DRAW_COLORS] = {Off, Red, Orange, Yellow, Green, Blue, Indigo};
uint32_t PuzzleColors[NUM_PUZZLE_COLORS] = {Off, Blue, Green, Red};

#define SNAKE_MOVE_INTERVAL_MS 500
#define OBSTACLE_SPAWN_INTERVAL_MS 3000
#define MAX_OBSTACLES 10
#define DEAD_BLINK_INTERVAL_MS 300
#define GAME_OVER_DELAY_MS 3000
#define GAME_OVER_PENALTY_MS 1000
#define GAME_OVER_MAX_DELAY_MS 15000

Snake snake;
bool obstaclesEnabled = false;
int obstacles[MAX_OBSTACLES];
int numObstacles = 0;
int foodCell = -1; // -1 = no food currently on board
bool snakeGameOver = false;
int snakeDeadCell = -1;
unsigned long snakeMoveIntervalMs = SNAKE_MOVE_INTERVAL_MS;
unsigned long obstacleSpawnIntervalMs = OBSTACLE_SPAWN_INTERVAL_MS;
unsigned long gameOverDelayMs = GAME_OVER_DELAY_MS;

unsigned long lastSnakeMoveMillis = 0;
unsigned long lastObstacleSpawnMillis = 0;
unsigned long lastDeadBlinkMillis = 0;
unsigned long lastGameOverInputMillis = 0;
bool deadBlinkOn = true;

uint32_t ObstacleColor = Red;
uint32_t FoodColor = Blue;

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

bool isFoodCell(int cell)
{
  return foodCell != -1 && foodCell == cell;
}

bool isObstacleCell(int cell)
{
  for (int i = 0; i < numObstacles; i++)
  {
    if (obstacles[i] == cell)
    {
      return true;
    }
  }

  return false;
}

bool isSnakeCell(int cell)
{
  return snake.contains(cell);
}

void spawnFood()
{
  if (foodCell != -1)
  {
    return;
  }

  int cell, attempts = 0;
  do
  {
    cell = random(NUM_KEYS);
    attempts++;
  } while ((isSnakeCell(cell) || isObstacleCell(cell)) && attempts < 20);

  if (!isSnakeCell(cell) && !isObstacleCell(cell))
  {
    log_i("Food spawned at: %d", cell);
    foodCell = cell;
  }
}

void removeFoodAt(int cell)
{
  if (foodCell == cell)
  {
    foodCell = -1;
  }
}

bool isAdjacentToHead(int cell)
{
  int headRow = indexToRow(snake.head());
  int headCol = indexToCol(snake.head());
  int row = indexToRow(cell);
  int col = indexToCol(cell);
  int rowDist = abs(row - headRow);
  int colDist = abs(col - headCol);
  return (rowDist + colDist) <= 1; // includes head cell itself (dist 0) and 4 orthogonal neighbors
}

void spawnObstacle()
{
  if (!obstaclesEnabled || numObstacles >= MAX_OBSTACLES)
  {
    return;
  }

  int cell, attempts = 0;
  do
  {
    cell = random(NUM_KEYS);
    attempts++;
  } while ((isSnakeCell(cell) || isObstacleCell(cell) || isFoodCell(cell) || isAdjacentToHead(cell)) && attempts < 20);

  if (!isSnakeCell(cell) && !isObstacleCell(cell) && !isFoodCell(cell) && !isAdjacentToHead(cell))
  {
    log_i("Spawned obstacle at %d", cell);
    obstacles[numObstacles++] = cell;
  }
}

void removeObstacleAt(int cell)
{
  for (int i = 0; i < numObstacles; i++)
  {
    if (obstacles[i] == cell)
    {
      obstacles[i] = obstacles[--numObstacles]; // swap-remove
      return;
    }
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

  // snake gets dimmer with each segment down to ~20% brightness at tail
  int red = 0x4F;
  int redDelta = (1.6 * red) / snake.size();
  int green = 0xFF;
  int greenDelta = (0.8 * green) / snake.size();
  for (int i = 0; i < snake.size(); i++)
  {
    uint32_t color = neoTrellis.pixels.Color(red, green, 0x00);
    neoTrellis.pixels.setPixelColor(snake[i], color);

    red = max(0, red - redDelta);
    green = max(40, green - greenDelta);
  }

  // blink what killed the snake
  if (snakeGameOver && snakeDeadCell != -1 && !deadBlinkOn)
  {
    neoTrellis.pixels.setPixelColor(snakeDeadCell, Off);
  }

  neoTrellis.pixels.show();
}

void resetSnakeGame()
{
  snake.clearTail();
  numObstacles = 0;
  foodCell = -1;
  snakeGameOver = false;
  lastSnakeMoveMillis = millis();
  lastObstacleSpawnMillis = millis();
}

void triggerGameOver(int deadCell)
{
  snakeGameOver = true;
  snakeDeadCell = deadCell;
  deadBlinkOn = true;
  lastDeadBlinkMillis = millis();
  lastGameOverInputMillis = millis();
  gameOverDelayMs = GAME_OVER_DELAY_MS; 

  // difficulty increase after each win
  if (deadCell < 0)
  {
    obstacleSpawnIntervalMs = 0.8 * obstacleSpawnIntervalMs;
    snakeMoveIntervalMs = 0.8 * snakeMoveIntervalMs;
    log_i("New difficulty: %d %d", obstacleSpawnIntervalMs, snakeMoveIntervalMs);
  }
}

void shrinkSnake()
{
  if (snake.size() > 1)
  {
    log_i("Player booped snake");
    snake.popTail();
  }
  else
  {
    // victory on snake 1, loss on snake 2
    log_i("Player squished snake!");
    triggerGameOver(obstaclesEnabled ? snake.head() : -1);
  }
}

void moveSnake()
{
  int nextCell = getSnakeNextCell(snake, foodCell);

  if (nextCell == -1)
  {
    log_w("Snake trapped: %d", snake.head());
    triggerGameOver(snake.head());
    return;
  }

  if (isObstacleCell(nextCell))
  {
    log_i("Snake hit obstacle: %d->%d", snake.head(), nextCell);
    triggerGameOver(nextCell);
    return;
  }

  if (isFoodCell(nextCell))
  {
    log_i("Snake ate food: %d->%d", snake.head(), nextCell);
    removeFoodAt(nextCell);
    snake.grow(nextCell);
  }
  else
  {
    log_i("Snake moved: %d->%d", snake.head(), nextCell);
    snake.move(nextCell);
  }

  // check game ending condition
  if (snake.size() == SNAKE_MAX_LEN)
  {
    // loss on snake 1, victory on snake 2
    log_i("Snake is big boi!");
    triggerGameOver(obstaclesEnabled ? -1 : snake.head());
    return;
  }
}

void snakeGameTick()
{
  if (snakeGameOver)
  {
    if (millis() - lastDeadBlinkMillis > DEAD_BLINK_INTERVAL_MS)
    {
      deadBlinkOn = !deadBlinkOn;
      lastDeadBlinkMillis = millis();
      drawSnakeGrid();
    }
    return;
  }

  spawnFood();

  if (millis() - lastObstacleSpawnMillis > obstacleSpawnIntervalMs)
  {
    spawnObstacle();
    lastObstacleSpawnMillis = millis();
  }

  if (millis() - lastSnakeMoveMillis > snakeMoveIntervalMs)
  {
    moveSnake();
    lastSnakeMoveMillis = millis();
  }

  drawSnakeGrid();
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
    case ModeSelect::DrawSelect:
      gameMode = GameMode::DrawMode;
      gameModeColors = DrawColors;
      gameModeNumColors = NUM_DRAW_COLORS;
      initRandomColorState();
      log_i("Game Mode: draw");
      break;
    case ModeSelect::PuzzleSelect:
      gameMode = GameMode::PuzzleMode;
      gameModeColors = PuzzleColors;
      gameModeNumColors = NUM_PUZZLE_COLORS;
      initRandomColorState();
      log_i("Game Mode: puzzle");
      break;
    case ModeSelect::SnakeSelect:
      gameMode = GameMode::SnakeMode;
      snake.clear();
      snake.pushHead(random(NUM_KEYS));
      obstaclesEnabled = false;
      log_i("Game Mode: snake");
      break;
    case ModeSelect::Snake2Select:
      gameMode = GameMode::Snake2Mode;
      snake.clear();
      snake.pushHead(random(NUM_KEYS));
      obstaclesEnabled = true;
      log_i("Game Mode: snake 2");
      break;
    case ModeSelect::BrightnessUp:
    case ModeSelect::BrightnessDn:
      delta = event.bit.NUM == ModeSelect::BrightnessUp ? 17 : -17;
      brightness = constrain(brightness + delta, 5, 100);
      neoTrellis.pixels.setBrightness(brightness);

      // TODO: factor into helper shared with setup
      neoTrellis.pixels.setPixelColor(ModeSelect::DrawSelect, Red);
      neoTrellis.pixels.setPixelColor(ModeSelect::PuzzleSelect, Violet);
      neoTrellis.pixels.setPixelColor(ModeSelect::SnakeSelect, YellowGreen);
      neoTrellis.pixels.setPixelColor(ModeSelect::Snake2Select, Green);
      neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessUp, Yellow);
      neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessDn, Orange);
      if (neoTrellis.pixels.getPixelColor(ModeSelect::WifiInit) != Off)
      {
        neoTrellis.pixels.setPixelColor(ModeSelect::WifiInit, Blue);
      }
      neoTrellis.pixels.show();
      log_i("Brightness: %d", brightness);
      break;
    case ModeSelect::WifiInit:
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
    case GameMode::Snake2Mode:
      if (snakeGameOver)
      {
        if (isSnakeCell(key))
        {
          if (millis() - lastGameOverInputMillis > gameOverDelayMs)
          {
            resetSnakeGame();
            tone(SPEAKER_PIN, 1500);
          }
          else
          {
            unsigned long nextDelay = gameOverDelayMs + GAME_OVER_PENALTY_MS;
            gameOverDelayMs = (nextDelay < GAME_OVER_MAX_DELAY_MS) ? nextDelay : GAME_OVER_MAX_DELAY_MS; // reset and increase delay each preemptive button press
            lastGameOverInputMillis = millis();
            log_i("Not resetting, need to wait %ds now", (int)(gameOverDelayMs / 1000.0));
          }
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
  neoTrellis.activateKey(ModeSelect::DrawSelect, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::PuzzleSelect, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::SnakeSelect, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::Snake2Select, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::BrightnessUp, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::BrightnessDn, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.activateKey(ModeSelect::WifiInit, SEESAW_KEYPAD_EDGE_RISING);
  neoTrellis.registerCallback(ModeSelect::DrawSelect, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::PuzzleSelect, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::SnakeSelect, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::Snake2Select, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::BrightnessUp, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::BrightnessDn, modeSelectButtonCallback);
  neoTrellis.registerCallback(ModeSelect::WifiInit, modeSelectButtonCallback);
  neoTrellis.pixels.setPixelColor(ModeSelect::DrawSelect, Red);
  neoTrellis.pixels.setPixelColor(ModeSelect::PuzzleSelect, Violet);
  neoTrellis.pixels.setPixelColor(ModeSelect::SnakeSelect, YellowGreen);
  neoTrellis.pixels.setPixelColor(ModeSelect::Snake2Select, Green);
  neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessUp, Yellow);
  neoTrellis.pixels.setPixelColor(ModeSelect::BrightnessDn, Orange);
  neoTrellis.pixels.setPixelColor(ModeSelect::WifiInit, Blue);

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
      neoTrellis.pixels.setPixelColor(ModeSelect::WifiInit, isBlue ? Blue : altColor);
      neoTrellis.pixels.show();

      lastToggleMillis = millis();
      toggleDelayMs = isBlue ? 1000 : 100;
    }
    delay(20);
  }

  // unregister callbacks for setup
  neoTrellis.unregisterCallback(ModeSelect::DrawSelect);
  neoTrellis.unregisterCallback(ModeSelect::PuzzleSelect);
  neoTrellis.unregisterCallback(ModeSelect::SnakeSelect);
  neoTrellis.unregisterCallback(ModeSelect::Snake2Select);
  neoTrellis.unregisterCallback(ModeSelect::BrightnessUp);
  neoTrellis.unregisterCallback(ModeSelect::BrightnessDn);
  neoTrellis.unregisterCallback(ModeSelect::WifiInit);

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

  if (gameMode == SnakeMode || gameMode == Snake2Mode)
  {
    snakeGameTick();
  }

  delay(20);
}
