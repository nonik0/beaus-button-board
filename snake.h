#pragma once

#include <Arduino.h>

#define SNAKE_MAX_LEN 16

struct Snake
{
    int cells[SNAKE_MAX_LEN];
    int count = 0;

    int size() const { return count; }
    bool empty() const { return count == 0; }
    bool full() const { return count == SNAKE_MAX_LEN; }

    int &operator[](int i) { return cells[i]; }
    const int &operator[](int i) const { return cells[i]; }

    int head() const { return cells[0]; }
    int tail() const { return cells[count - 1]; }

    bool contains(int cell) const
    {
        for (int i = 0; i < count; i++)
        {
            if (cells[i] == cell)
            {
                return true;
            }
        }
        return false;
    }

    bool isHead(int cell) const { return count > 0 && cell == head(); }
    bool isTail(int cell) const { return count > 0 && cell == tail(); }

    void clear()
    {
        count = 0;
    }

    void clearTail()
    {
        count = 1;
    }

    void pushHead(int cell)
    {
        if (full())
        {
            return;
        }

        for (int i = count; i > 0; i--)
        {
            cells[i] = cells[i - 1];
        }

        cells[0] = cell;
        count++;
    }

    void popTail()
    {
        if (count > 0)
        {
            count--;
        }
    }

    void move(int nextCell)
    {
        for (int i = count - 1; i > 0; i--)
        {
            cells[i] = cells[i - 1];
        }

        cells[0] = nextCell;
    }

    void grow(int nextCell)
    {
        if (!full())
        {
            count++;
        }

        move(nextCell);
    }
};