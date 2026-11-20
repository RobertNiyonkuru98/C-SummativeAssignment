#include <LiquidCrystal.h>

#define MAX_CAPACITY 5
#define DEBOUNCE_MS 200
#define LCD_REFRESH_MS 500

#define ENTRY_BUTTON 2
#define EXIT_BUTTON 3
#define GREEN_LED 6
#define RED_LED 7

LiquidCrystal lcd(12, 11, 5, 4, 9, 8);

typedef struct {
  int people_inside;
  int max_capacity;
  int total_entries;
  int total_exits;
  int rejected_entries;
  int rejected_exits;
} Occupancy;

typedef struct {
  unsigned long at_ms;
  char text[48];
} EventRecord;

typedef struct {
  EventRecord *items;
  int count;
  int capacity;
} EventLog;

Occupancy *occupancy = NULL;
EventLog *events = NULL;

unsigned long last_entry_press = 0;
unsigned long last_exit_press = 0;
int entry_was_pressed = 0;
int exit_was_pressed = 0;

unsigned long last_lcd_update = 0;
unsigned long last_stats = 0;

#define STATS_INTERVAL_MS 10000

int allocate_state(void) {
  occupancy = (Occupancy *)calloc(1, sizeof(Occupancy));
  if (occupancy == NULL) return 0;
  occupancy->people_inside = 0;
  occupancy->max_capacity = MAX_CAPACITY;
  occupancy->total_entries = 0;
  occupancy->total_exits = 0;
  occupancy->rejected_entries = 0;
  occupancy->rejected_exits = 0;
  return 1;
}

int allocate_log(void) {
  events = (EventLog *)calloc(1, sizeof(EventLog));
  if (events == NULL) return 0;
  events->capacity = 4;
  events->count = 0;
  events->items = (EventRecord *)calloc(events->capacity, sizeof(EventRecord));
  return events->items != NULL;
}

int add_event(const char *text) {
  EventRecord *grown;
  int new_capacity;
  int i;

  if (events == NULL) return 0;

  if (events->count >= events->capacity) {
    new_capacity = events->capacity * 2;
    grown = (EventRecord *)realloc(events->items,
                                   new_capacity * sizeof(EventRecord));
    if (grown == NULL) {
      Serial.println(F("[mem] could not grow event log"));
      return 0;
    }
    events->items = grown;
    events->capacity = new_capacity;
  }

  events->items[events->count].at_ms = millis();
  for (i = 0; i < 47 && text[i] != '\0'; i++) {
    events->items[events->count].text[i] = text[i];
  }
  events->items[events->count].text[i] = '\0';
  events->count++;
  return 1;
}

void update_leds(void) {
  if (occupancy->people_inside >= occupancy->max_capacity) {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);
  } else {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
  }
}

void handle_buttons(void) {
  unsigned long now = millis();
  int entry_raw = digitalRead(ENTRY_BUTTON);
  int exit_raw  = digitalRead(EXIT_BUTTON);

  // Debounce: detect falling edge (active-low with PULLUP)
  if (entry_raw == LOW && !entry_was_pressed &&
      (now - last_entry_press) >= DEBOUNCE_MS) {
    entry_was_pressed = 1;
    last_entry_press = now;

    if (occupancy->people_inside < occupancy->max_capacity) {
      occupancy->people_inside++;
      occupancy->total_entries++;
      char buf[48];
      snprintf(buf, sizeof(buf), "ENTRY #%d (inside: %d)",
               occupancy->total_entries, occupancy->people_inside);
      add_event(buf);
      Serial.println(buf);
    } else {
      occupancy->rejected_entries++;
      char buf[48];
      snprintf(buf, sizeof(buf), "REJECTED (full, inside: %d)",
               occupancy->people_inside);
      add_event(buf);
      Serial.println(buf);
    }
    update_leds();
  } else if (entry_raw == HIGH) {
    entry_was_pressed = 0;
  }

  if (exit_raw == LOW && !exit_was_pressed &&
      (now - last_exit_press) >= DEBOUNCE_MS) {
    exit_was_pressed = 1;
    last_exit_press = now;

    if (occupancy->people_inside > 0) {
      occupancy->people_inside--;
      occupancy->total_exits++;
      char buf[48];
      snprintf(buf, sizeof(buf), "EXIT #%d (inside: %d)",
               occupancy->total_exits, occupancy->people_inside);
      add_event(buf);
      Serial.println(buf);
    } else {
      occupancy->rejected_exits++;
      char buf[48];
      snprintf(buf, sizeof(buf), "REJECTED (empty)");
      add_event(buf);
      Serial.println(buf);
    }
    update_leds();
  } else if (exit_raw == HIGH) {
    exit_was_pressed = 0;
  }
}

void update_lcd(void) {
  unsigned long now = millis();
  if (now - last_lcd_update < LCD_REFRESH_MS) return;
  last_lcd_update = now;

  lcd.setCursor(0, 0);
  lcd.print(F("Inside: "));
  lcd.print(occupancy->people_inside);
  lcd.print(F("/"));
  lcd.print(occupancy->max_capacity);

  lcd.setCursor(0, 1);
  char line[17];
  snprintf(line, sizeof(line), "Free: %d  %-5s",
           occupancy->max_capacity - occupancy->people_inside,
           occupancy->people_inside >= occupancy->max_capacity ? "FULL!" : "Open");
  lcd.print(line);
}

void print_stats(void) {
  unsigned long now = millis();
  if (now - last_stats < STATS_INTERVAL_MS) return;
  last_stats = now;

  Serial.println(F("--- Stats ---"));
  Serial.print(F("Inside: "));        Serial.println(occupancy->people_inside);
  Serial.print(F("Entries: "));       Serial.println(occupancy->total_entries);
  Serial.print(F("Exits: "));         Serial.println(occupancy->total_exits);
  Serial.print(F("Rejected in: "));   Serial.println(occupancy->rejected_entries);
  Serial.print(F("Rejected out: "));  Serial.println(occupancy->rejected_exits);

  // the event log is held in dynamic memory, so show the most recent entries
  int first = events->count > 5 ? events->count - 5 : 0;
  Serial.print(F("Recent events ("));
  Serial.print(events->count);
  Serial.println(F(" logged):"));
  for (int i = first; i < events->count; i++) {
    Serial.print(F("  "));
    Serial.print(events->items[i].at_ms);
    Serial.print(F(" ms - "));
    Serial.println(events->items[i].text);
  }

  Serial.println(F("--------------"));
}

void setup() {
  Serial.begin(9600);

  if (!allocate_state()) {
    Serial.println(F("FATAL: state alloc"));
    while (1);
  }
  if (!allocate_log()) {
    Serial.println(F("FATAL: log alloc"));
    while (1);
  }

  pinMode(ENTRY_BUTTON, INPUT_PULLUP);
  pinMode(EXIT_BUTTON,  INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED,   OUTPUT);

  lcd.begin(16, 2);
  lcd.print(F("Occupancy Tracker"));
  delay(1500);
  lcd.clear();

  add_event("System started");   
  update_leds();
}

void loop() {
  handle_buttons();
  update_lcd();
  print_stats();
}