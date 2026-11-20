/* Logic test for the occupancy sketch - drives setup() and loop() with
 * simulated button presses and checks the result. The sketch is included
 * verbatim, so these are the real functions running, not a copy.
 *
 *   g++ -Wall -std=c++17 -I docs/evidence docs/evidence/occupancy_sim_test.cpp -o occ_test
 *   ./occ_test
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

struct SerialStub {
  void begin(long) {}
  void print(const char *s) { fputs(s, stdout); }
  void print(char c) { fputc(c, stdout); }
  void print(int v) { printf("%d", v); }
  void print(unsigned long v) { printf("%lu", v); }
  void println(const char *s) { printf("%s\n", s); }
  void println(int v) { printf("%d\n", v); }
  void println(unsigned long v) { printf("%lu\n", v); }
  void println(void) { printf("\n"); }
};
SerialStub Serial;

inline const char *F(const char *s) { return s; }

/* LiquidCrystal comes from docs/evidence/LiquidCrystal.h via -I docs/evidence */
#include "../../occupancymonitor.ino"

static int failures = 0;
static int checks = 0;

static void check(const char *label, bool ok, const char *detail = "") {
  checks++;
  if (!ok)
    failures++;
  printf("  %-48s %s%s%s\n", label, ok ? "PASS" : "*** FAIL ***",
         detail[0] ? "  " : "", detail);
}

static void release_all(void) {
  for (int i = 0; i < 32; i++)
    g_button[i] = HIGH;
}

static std::string lcd_line(int row) {
  return std::string(lcd.screen[row], strnlen(lcd.screen[row], 16));
}

/* The display only repaints every LCD_REFRESH_MS, so give it time to catch up
   before asserting on what it shows. */
static void settle(void) {
  g_now += 600;
  loop();
}

static void press(int pin) {
  g_button[pin] = LOW;
  loop();
  g_button[pin] = HIGH;
  loop();
}

int main(void) {
  release_all();

  printf("\n===== OCCUPANCY SKETCH LOGIC TEST =====\n\n");
  setup();

  printf("\n--- Scenario 1: power on, facility empty ---\n");
  check("occupancy starts at 0", occupancy->people_inside == 0);
  check("capacity is 5", occupancy->max_capacity == 5);
  check("green LED is ON (room available)", g_output[6] == HIGH);
  check("red LED is OFF", g_output[7] == LOW);
  settle();
  check("LCD line 1 shows Inside: 0/5", lcd_line(0).find("Inside: 0/5") == 0,
        lcd_line(0).c_str());
  check("LCD line 2 shows Free: 5", lcd_line(1).find("Free: 5") == 0,
        lcd_line(1).c_str());
  check("LCD line 2 shows Open", lcd_line(1).find("Open") != std::string::npos,
        lcd_line(1).c_str());

  printf("\n--- Scenario 2: three people enter ---\n");
  for (int i = 0; i < 3; i++) {
    g_now += 300;
    press(2);
  }
  settle();
  check("occupancy is 3", occupancy->people_inside == 3);
  check("total entries counted", occupancy->total_entries == 3);
  check("green LED still ON", g_output[6] == HIGH);
  check("LCD shows Inside: 3/5", lcd_line(0).find("Inside: 3/5") == 0,
        lcd_line(0).c_str());
  check("LCD shows 2 free places", lcd_line(1).find("Free: 2") == 0,
        lcd_line(1).c_str());

  printf("\n--- Scenario 3: fill the facility to capacity ---\n");
  for (int i = 0; i < 2; i++) {
    g_now += 300;
    press(2);
  }
  settle();
  check("occupancy reached capacity 5", occupancy->people_inside == 5);
  check("green LED is OFF", g_output[6] == LOW);
  check("red LED is ON (full)", g_output[7] == HIGH);
  check("LCD shows Inside: 5/5", lcd_line(0).find("Inside: 5/5") == 0,
        lcd_line(0).c_str());
  check("LCD shows Free: 0", lcd_line(1).find("Free: 0") == 0,
        lcd_line(1).c_str());
  check("LCD shows FULL!", lcd_line(1).find("FULL!") != std::string::npos,
        lcd_line(1).c_str());

  printf("\n--- Scenario 4: entry refused while full ---\n");
  g_now += 300;
  press(2);
  settle();
  check("occupancy is still 5", occupancy->people_inside == 5);
  check("rejected entry counted", occupancy->rejected_entries == 1);
  check("red LED still ON", g_output[7] == HIGH);

  printf("\n--- Scenario 5: bounce is ignored ---\n");
  g_now += 300;
  press(3);                       // a real exit, occupancy 5 -> 4
  g_now += 50;
  press(3);                       // bounce 50 ms later, must be ignored
  settle();
  check("only one exit was accepted", occupancy->people_inside == 4,
        (std::to_string(occupancy->people_inside) + " inside").c_str());
  check("total exits is 1", occupancy->total_exits == 1);
  check("green LED back ON", g_output[6] == HIGH);

  printf("\n--- Scenario 6: empty the room, then try to leave again ---\n");
  for (int i = 0; i < 4; i++) {
    g_now += 300;
    press(3);
  }
  g_now += 300;
  press(3);                       // refused, nobody inside
  settle();
  check("occupancy reached 0", occupancy->people_inside == 0);
  check("rejected exit counted", occupancy->rejected_exits == 1);
  check("LCD shows Free: 5 again", lcd_line(1).find("Free: 5") == 0,
        lcd_line(1).c_str());
  check("LCD shows Open again", lcd_line(1).find("Open") != std::string::npos,
        lcd_line(1).c_str());

  printf("\n--- Scenario 7: the display refreshes on its own timer ---\n");
  g_now += 300;
  press(2);                       // occupancy 1, but no repaint yet
  g_now += 100;
  loop();
  check("LCD still stale 100 ms after the change",
        lcd_line(0).find("Inside: 0/5") == 0, lcd_line(0).c_str());
  g_now += 500;
  loop();
  check("LCD catches up after 500 ms", lcd_line(0).find("Inside: 1/5") == 0,
        lcd_line(0).c_str());

  printf("\n--- Scenario 8: the event log grows as it fills ---\n");
  check("event log expanded past its first 4 slots", events->capacity > 4,
        (std::to_string(events->capacity) + " slots").c_str());
  check("every accepted and refused action was logged",
        events->count == occupancy->total_entries + occupancy->total_exits +
                            occupancy->rejected_entries + occupancy->rejected_exits + 1,
        (std::to_string(events->count) + " events").c_str());

  printf("\n===== RESULT: %d checks, %d failure(s) =====\n\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
