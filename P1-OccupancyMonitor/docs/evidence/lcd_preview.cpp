/* LCD preview for the occupancy sketch.
 *
 * Same shims as the logic test, but this one does not check anything - it draws
 * what the 16x2 display and the two LEDs would show after a sequence of button
 * presses, and echoes the Serial Monitor output for each step. It is the fastest
 * way to see the sketch behave without Tinkercad, an Arduino, or a breadboard.
 *
 *   g++ -Wall -std=c++17 -I docs/evidence docs/evidence/lcd_preview.cpp -o lcd_preview
 *   ./lcd_preview
 */

#include <cstdio>
#include <cstring>
#include <string>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2

static unsigned long g_now = 0;
unsigned long millis(void) { return g_now; }
void delay(unsigned long ms) { g_now += ms; }

static int g_button[32];
static int g_output[32];

void pinMode(int, int) {}
void digitalWrite(int pin, int value) {
  if (pin >= 0 && pin < 32)
    g_output[pin] = value;
}
int digitalRead(int pin) {
  if (pin >= 0 && pin < 32)
    return g_button[pin];
  return HIGH;
}

static std::string g_serial;

struct SerialStub {
  void begin(long) {}
  void print(const char *s) { g_serial += s; }
  void print(char c) { g_serial += c; }
  void print(int v) { g_serial += std::to_string(v); }
  void print(unsigned long v) { g_serial += std::to_string(v); }
  void println(const char *s) { g_serial += s; g_serial += "\n"; }
  void println(int v) { g_serial += std::to_string(v); g_serial += "\n"; }
  void println(unsigned long v) { g_serial += std::to_string(v); g_serial += "\n"; }
  void println(void) { g_serial += "\n"; }
};
SerialStub Serial;

inline const char *F(const char *s) { return s; }

/* LiquidCrystal comes from docs/evidence/LiquidCrystal.h via -I docs/evidence */
#include "../../occupancymonitor.ino"

static void release_all(void) {
  for (int i = 0; i < 32; i++)
    g_button[i] = HIGH;
}

static void print_serial(void) {
  if (g_serial.empty())
    return;
  printf("  Serial Monitor:\n");
  size_t start = 0;
  while (start < g_serial.size()) {
    size_t end = g_serial.find('\n', start);
    if (end == std::string::npos)
      end = g_serial.size();
    printf("    %s\n", g_serial.substr(start, end - start).c_str());
    start = end + 1;
  }
  g_serial.clear();
}

static void print_lcd(void) {
  printf("  LCD:\n");
  printf("  +----------------+\n");
  for (int row = 0; row < 2; row++) {
    printf("  |");
    for (int col = 0; col < 16; col++) {
      char c = lcd.screen[row][col];
      putchar(c == '\0' ? ' ' : c);
    }
    printf("|\n");
  }
  printf("  +----------------+\n");
  printf("  Green LED: %-3s   Red LED: %-3s\n",
         g_output[6] == HIGH ? "ON" : "off", g_output[7] == HIGH ? "ON" : "off");
}

static void step(const char *caption) {
  printf("\n%s\n", caption);
  print_serial();
  print_lcd();
}

static void press(int pin) {
  g_button[pin] = LOW;
  loop();
  g_button[pin] = HIGH;
  loop();
}

int main(void) {
  release_all();

  printf("============================================================\n");
  printf(" OCCUPANCY MONITOR - LCD PREVIEW (no hardware required)\n");
  printf("============================================================\n");

  setup();
  g_now += 600;                         // let the display paint once
  loop();
  step("POWER ON - the room is empty");

  for (int i = 1; i <= 5; i++) {
    g_now += 600;                       // more than the 200 ms debounce
    press(2);                           // D2 = entry button
    char caption[64];
    snprintf(caption, sizeof caption, "ENTRY press %d", i);
    step(caption);
  }

  g_now += 600;
  press(2);
  step("ENTRY press 6 - refused, the facility is full");

  for (int i = 1; i <= 3; i++) {
    g_now += 600;
    press(3);                           // D3 = exit button
    char caption[64];
    snprintf(caption, sizeof caption, "EXIT press %d", i);
    step(caption);
  }

  for (int i = 4; i <= 5; i++) {
    g_now += 600;
    press(3);
    char caption[64];
    snprintf(caption, sizeof caption, "EXIT press %d", i);
    step(caption);
  }

  g_now += 600;
  press(3);
  step("EXIT press 6 - refused, nobody is inside");

  g_now += 10000;                     // cross the statistics interval
  loop();
  step("10 seconds later - the statistics and the event log print on their own");

  printf("\n============================================================\n");
  printf(" End of preview. Capacity was never exceeded and never went\n");
  printf(" below zero, and the indicators followed the occupancy.\n");
  printf("============================================================\n");
  return 0;
}
