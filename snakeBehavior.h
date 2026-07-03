#pragma once

#include <Arduino.h>

#include "snake.h"

// #define HAMILTON_SEARCH_LIMIT 5000 // generous for a 16-cell board, keeps ESP32 responsive
// #define HAMILTON_THRESHOLD 8       // try building a full-board cycle once snake is this long

// bool hamiltonActive = false;
// int hamiltonIndex[NUM_KEYS]; // hamiltonIndex[cell] = position in the cycle, -1 if not part of it
// int hamiltonSearchCount = 0;

#define NUM_KEYS 16
#define NUM_ROWS 4
#define NUM_COLS 4
#define MAX_PATH_LEN NUM_KEYS            // plain shortest paths never exceed board size
#define MAX_LONG_PATH_LEN (NUM_KEYS * 3) // longerPath can triple-expand each step with detours

// precomputed once, indexed 0..NUM_KEYS-1
const int rowLookup[NUM_KEYS] = {
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3};

const int colLookup[NUM_KEYS] = {
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3};

enum Direction
{
    Up,
    Down,
    Left,
    Right,
};

const Direction Directions[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};

inline int coordToIndex(int row, int col)
{
    return row * NUM_COLS + col;
}

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

Direction oppositeDirection(Direction dir)
{
    switch (dir)
    {
    case Direction::Up:
        return Direction::Down;
    case Direction::Down:
        return Direction::Up;
    case Direction::Left:
        return Direction::Right;
    default:
        return Direction::Left; // Direction::Right
    }
}

Direction directionBetween(int fromCell, int toCell)
{
    int fromRow = indexToRow(fromCell), fromCol = indexToCol(fromCell);
    int toRow = indexToRow(toCell), toCol = indexToCol(toCell);

    if (toRow == fromRow - 1)
        return Direction::Up;
    if (toRow == fromRow + 1)
        return Direction::Down;
    if (toCol == fromCol - 1)
        return Direction::Left;
    return Direction::Right; // toCol == fromCol + 1
}

int distance(int a, int b)
{
    return abs(indexToRow(a) - indexToRow(b)) + abs(indexToCol(a) - indexToCol(b));
}

int getAdjacent(int fromIndex, Direction dir)
{
    if (fromIndex < 0 || fromIndex >= NUM_KEYS)
    {
        return -1;
    }

    int row = indexToRow(fromIndex);
    int col = indexToCol(fromIndex);

    switch (dir)
    {
    case Direction::Up:
        row -= 1;
        break;
    case Direction::Down:
        row += 1;
        break;
    case Direction::Left:
        col -= 1;
        break;
    case Direction::Right:
        col += 1;
        break;
    }

    if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS)
    {
        return -1;
    }

    return coordToIndex(row, col);
}

int getAdjacentCells(int index, int outCells[4])
{
    int row = indexToRow(index), col = indexToCol(index);
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

bool isOutOfBounds(int index)
{
    int row = indexToRow(index);
    int col = indexToCol(index);
    return row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS;
}

bool isReachable(Snake snake, int index)
{
    return !isOutOfBounds(index) && (!snake.contains(index) || snake.isTail(index));
}

int getReachableNeighbors(const Snake &snake, int index, const bool visited[NUM_KEYS], int outNeighbors[4])
{
    int reachable = 0;

    int adjacent[4];
    int n = getAdjacentCells(index, adjacent);
    for (int i = 0; i < n; i++)
    {
        int next = adjacent[i];
        if (isReachable(snake, next) && !visited[next])
            outNeighbors[reachable++] = next;
    }

    // Fisher-Yates shuffle to reduce search bias
    for (int i = reachable - 1; i > 0; i--)
    {
        int j = random(i + 1);
        int tmp = outNeighbors[i];
        outNeighbors[i] = outNeighbors[j];
        outNeighbors[j] = tmp;
    }

    return reachable;
}

int getShortestPath(const Snake &snake, int dest, int outPath[MAX_PATH_LEN])
{
    int start = snake.head();
    if (start == dest)
    {
        return 0;
    }

    bool visited[NUM_KEYS] = {};
    visited[start] = true;

    int parentCell[NUM_KEYS];
    for (int i = 0; i < NUM_KEYS; i++)
        parentCell[i] = -1;

    int queue[NUM_KEYS];
    int qHead = 0, qTail = 0;
    queue[qTail++] = start;

    while (qHead != qTail)
    {
        int cur = queue[qHead++];
        if (cur == dest)
            break;

        int neighbor[4];
        int n = getReachableNeighbors(snake, cur, visited, neighbor);
        for (int i = 0; i < n; i++)
        {
            int index = neighbor[i];
            queue[qTail++] = index;
            parentCell[index] = cur;
            visited[index] = true;
        }
    }

    if (!visited[dest])
    {
        return -1;
    }

    // walks shortest path back to start
    int path[MAX_PATH_LEN];
    int len = 0;
    int cur = dest;
    while (cur != start)
    {
        int prev = parentCell[cur];
        path[len++] = cur;
        cur = prev;
    }

    // reverse path
    for (int i = 0; i < len; i++)
    {
        outPath[i] = path[len - 1 - i];
    }
    return len;
}

int getLongerPath(const Snake &snake, int foodIndex, int dest, int outPath[MAX_LONG_PATH_LEN])
{
    int shortestPath[MAX_PATH_LEN];
    int shortestLen = getShortestPath(snake, dest, shortestPath);
    if (shortestLen <= 0)
    {
        return shortestLen;
    }

    int len = 0;
    int cur = snake.head();
    int futureTailCell = snake.size() > 1 ? snake[snake.size() - 2] : -1;

    for (int i = 0; i < shortestLen; i++)
    {
        int next = shortestPath[i];
        Direction dir = directionBetween(cur, next);

        Direction testDirs[2];
        if (dir == Direction::Up || dir == Direction::Down)
        {
            testDirs[0] = Direction::Left;
            testDirs[1] = Direction::Right;
        }
        else
        {
            testDirs[0] = Direction::Up;
            testDirs[1] = Direction::Down;
        }

        bool extended = false;
        for (int t = 0; t < 2 && !extended; t++)
        {
            Direction testDir = testDirs[t];
            int curExt = getAdjacent(cur, testDir);
            int nextExt = getAdjacent(next, testDir);

            bool canExtCur = isReachable(snake, curExt) && curExt != foodIndex;
            bool canExtNext = isReachable(snake, nextExt) || nextExt == futureTailCell;

            if (canExtCur && canExtNext)
            {
                outPath[len++] = getAdjacent(cur, testDir);
                outPath[len++] = getAdjacent(outPath[len - 1], dir);
                outPath[len++] = getAdjacent(outPath[len - 1], oppositeDirection(testDir));
                extended = true;
                break;
            }
        }

        if (!extended)
        {
            outPath[len++] = next;
        }

        cur = next;
    }

    return len;
}

int getSnakeNextCell(const Snake &snake, int foodCell)
{
    bool visited[NUM_KEYS] = {};
    int neighbors[4];
    int n = getReachableNeighbors(snake, snake.head(), visited, neighbors);
    if (n <= 0)
    {
        return -1;
    }

    // find shortest path to food
    if (foodCell != -1)
    {
        int foodPath[MAX_PATH_LEN];
        int foodPathLen = getShortestPath(snake, foodCell, foodPath);
        if (foodPathLen > 0)
        {
            // move snake along shortest path to eat food
            Snake testSnake = snake; // TODO make sure this is a copy not ref, const above maybe helps
            for (int i = 0; i < foodPathLen; i++)
            {
                if (foodPath[i] == foodCell)
                {
                    testSnake.grow(foodPath[i]);
                }
                else
                {
                    testSnake.move(foodPath[i]);
                }
            }

            // if snake can still reach tail after eating then follow path
            int tailPath[MAX_PATH_LEN];
            bool isPathToTail = getShortestPath(testSnake, testSnake.tail(), tailPath) > 0;
            if (isPathToTail > 0)
            {
                log_i("Shortest path to food: %d", foodPathLen);
                return foodPath[0];
            }
        }
    }

    // no safe path to food, try moving along a longer path to tail
    int longerPath[MAX_LONG_PATH_LEN];
    int longerPathLen = getLongerPath(snake, foodCell, snake.tail(), longerPath);
    if (longerPathLen > 0)
    {
        log_i("Longer path to tail: %d", longerPath[0]);
        return longerPath[0];
    }

    // no path to tail either, just move further away from the food
    int furthestCell = neighbors[0];
    int furthestDist = foodCell != -1 ? distance(neighbors[0], foodCell) : 0;
    for (int i = 1; i < n; i++)
    {
        int dist = foodCell != -1 ? distance(neighbors[i], foodCell) : 0;
        if (dist > furthestDist)
        {
            furthestDist = dist;
            furthestCell = neighbors[i];
        }
    }

    log_i("Moving away from food: %d", furthestCell);
    return furthestCell;
}