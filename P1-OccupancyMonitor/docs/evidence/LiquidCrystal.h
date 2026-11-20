#ifndef MOCK_LIQUID_CRYSTAL_H
#define MOCK_LIQUID_CRYSTAL_H

#include <cstdio>
#include <cstring>

struct LiquidCrystal {
  char screen[2][17];
  int cursor_row, cursor_col;

  LiquidCrystal(int, int, int, int, int, int) { clear(); }

  void begin(int, int) { clear(); }

  void clear(void) {
    memset(screen, ' ', sizeof screen);
    for (int r = 0; r < 2; r++)
      screen[r][16] = '\0';
    cursor_row = 0;
    cursor_col = 0;
  }

  void setCursor(int col, int row) {
    cursor_col = col;
    cursor_row = row;
  }

  void print(const char *s) {
    while (*s != '\0' && cursor_row < 2) {
      if (cursor_col >= 0 && cursor_col < 16)
        screen[cursor_row][cursor_col] = *s;
      cursor_col++;
      s++;
    }
  }

  void print(int value) {
    char buffer[16];
    snprintf(buffer, sizeof buffer, "%d", value);
    print(buffer);
  }
};

#endif
