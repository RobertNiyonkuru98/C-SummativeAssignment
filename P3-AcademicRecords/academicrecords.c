#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUDENT_ID "100"
#define NAME_LEN 32
#define PROG_LEN 40
#define CODE_LEN 12
#define COURSE_NAME_LEN 48
#define MAX_COURSES 8
#define INITIAL_CAP 4
#define DATA_FILE "academic_records.txt"
#define PASS_PERCENT 50.0

typedef struct {
  char code[CODE_LEN];
  char name[COURSE_NAME_LEN];
  int marks;
  int max_marks;
} Course;

typedef struct {
  int id;
  char first[NAME_LEN];
  char last[NAME_LEN];
  char programme[PROG_LEN];
  int year;
  int course_count;
  Course courses[MAX_COURSES];
} Student;

typedef struct {
  Student *items;
  int count;
  int capacity;
} Record;

typedef void (*Operation)(Record *);

int g_running = 1;

// ---------------- input helpers ----------------

// one line, newline removed; 1 = ok, 0 = end of input, -1 = line too long
int read_line(char *buf, int size) {
  int len, ch, overflow = 0;

  if (fgets(buf, size, stdin) == NULL)
    return buf[0] = '\0', 0;
  len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n')
    return buf[len - 1] = '\0', 1;
  while ((ch = getchar()) != '\n' && ch != EOF)
    overflow = 1;
  return overflow ? -1 : 1;
}

int read_int(const char *prompt, int *out) {
  char buf[32], *end;
  int status;

  printf("%s", prompt);
  status = read_line(buf, 32);
  if (status != 1)
    return status == 0 ? 0 : -1;
  *out = (int)strtol(buf, &end, 10);
  while (*end == ' ' || *end == '\t')
    end++;
  return (end == buf || *end != '\0') ? -1 : 1;
}

// repeats until the number is inside [low, high]; 0 = end of input
int ask_int(const char *prompt, int low, int high, const char *not_number,
            const char *out_of_range, int *out) {
  int status;

  for (;;) {
    status = read_int(prompt, out);
    if (status == 0)
      return 0;
    if (status == -1) {
      printf("%s", not_number);
      continue;
    }
    if (*out < low || *out > high) {
      printf("%s", out_of_range);
      continue;
    }
    return 1;
  }
}

// reads a non-empty text field; 0 = end of input
int ask_text(const char *prompt, char *out, int size) {
  int status;

  for (;;) {
    printf("%s", prompt);
    status = read_line(out, size);
    if (status == 0)
      return 0;
    if (status == -1) {
      printf("  Invalid: too long (maximum %d characters).\n", size - 1);
      continue;
    }
    if (out[0] == '\0') {
      printf("  Invalid: this field cannot be empty.\n");
      continue;
    }
    return 1;
  }
}

// case-insensitive compare built on strcmp
int compare_insensitive(const char *a, const char *b) {
  char la[PROG_LEN], lb[PROG_LEN];
  int i;

  for (i = 0; i + 1 < PROG_LEN && a[i]; i++)
    la[i] = (char)tolower((unsigned char)a[i]);
  la[i] = '\0';
  for (i = 0; i + 1 < PROG_LEN && b[i]; i++)
    lb[i] = (char)tolower((unsigned char)b[i]);
  lb[i] = '\0';
  return strcmp(la, lb);
}

// 1 when `needle` appears inside `haystack`, ignoring case
int contains_insensitive(const char *haystack, const char *needle) {
  char lo_hay[PROG_LEN], lo_need[PROG_LEN];

  for (int i = 0; i + 1 < PROG_LEN && haystack[i]; i++)
    lo_hay[i] = (char)tolower((unsigned char)haystack[i]);
  lo_hay[strlen(haystack)] = '\0';
  for (int i = 0; i + 1 < PROG_LEN && needle[i]; i++)
    lo_need[i] = (char)tolower((unsigned char)needle[i]);
  lo_need[strlen(needle)] = '\0';

  return strstr(lo_hay, lo_need) != NULL;
}

// ---------------- record and memory helpers ----------------

int ensure_capacity(Record *list, int needed) {
  int new_cap = list->capacity;
  Student *grown;

  if (needed <= list->capacity)
    return 1;
  while (new_cap < needed)
    new_cap *= 2;
  grown = realloc(list->items, new_cap * sizeof(Student));
  if (grown == NULL) {
    printf("  [mem] could not grow the record list - operation cancelled.\n");
    return 0;
  }
  printf("  [mem] records %d -> %d slots\n", list->capacity, new_cap);
  list->items = grown;
  list->capacity = new_cap;
  return 1;
}

int shrink_if_sparse(Record *list) {
  int new_cap;
  Student *shrunk;

  if (list->capacity <= INITIAL_CAP || list->count > list->capacity / 4)
    return 1;
  new_cap = list->capacity / 2 < INITIAL_CAP ? INITIAL_CAP : list->capacity / 2;
  shrunk = realloc(list->items, new_cap * sizeof(Student));
  if (shrunk == NULL)
    return 1;
  printf("  [mem] records %d -> %d slots (shrunk)\n", list->capacity, new_cap);
  list->items = shrunk;
  list->capacity = new_cap;
  return 1;
}

int find_index_by_id(const Record *list, int id) {
  int i;

  for (i = 0; i < list->count; i++)
    if (list->items[i].id == id)
      return i;
  return -1;
}

// average percentage across a student's courses; 0 when nothing is registered
double student_average(const Student *s) {
  int i;
  int total_marks = 0, total_max = 0;

  if (s->course_count == 0)
    return 0.0;
  for (i = 0; i < s->course_count; i++) {
    total_marks += s->courses[i].marks;
    total_max += s->courses[i].max_marks;
  }
  if (total_max == 0)
    return 0.0;
  return 100.0 * total_marks / total_max;
}

int courses_passed(const Student *s) {
  int i, passed = 0;

  for (i = 0; i < s->course_count; i++)
    if (100.0 * s->courses[i].marks / s->courses[i].max_marks >= PASS_PERCENT)
      passed++;
  return passed;
}

void print_student_line(const Student *s) {
  printf("  %-6d %-12s %-12s %-24s %2d %6.1f%%\n", s->id, s->first, s->last,
         s->programme, s->year, student_average(s));
}

void print_student_table_header(void) {
  printf("  %-6s %-12s %-12s %-24s %2s %7s\n", "ID", "First", "Last",
         "Programme", "Yr", "Avg");
  printf("  %-6s %-12s %-12s %-24s %2s %7s\n", "------", "------------",
         "------------", "------------------------", "--", "-------");
}

// ---------------- menu operations ----------------

void register_student(Record *list) {
  Student s;
  int value, index;

  printf("\n  --- Register a new student ---\n");
  if (!ensure_capacity(list, list->count + 1))
    return;

  for (;;) {
    if (!ask_int("  Student ID: ", 1, 2000000000,
                 "  Invalid: enter a whole number.\n",
                 "  Invalid: the ID must be positive.\n", &value)) {
      printf("\n  Input closed - registration cancelled.\n");
      return;
    }
    index = find_index_by_id(list, value);
    if (index >= 0) {
      printf("  Rejected: ID %d already belongs to %s %s.\n", value,
             list->items[index].first, list->items[index].last);
      continue;
    }
    s.id = value;
    break;
  }

  if (!ask_text("  First name: ", s.first, NAME_LEN) ||
      !ask_text("  Last name: ", s.last, NAME_LEN) ||
      !ask_text("  Programme: ", s.programme, PROG_LEN)) {
    printf("\n  Input closed - registration cancelled.\n");
    return;
  }
  if (!ask_int("  Year of study (1-6): ", 1, 6,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the year must be between 1 and 6.\n", &value)) {
    printf("\n  Input closed - registration cancelled.\n");
    return;
  }
  s.year = value;
  s.course_count = 0;

  list->items[list->count++] = s;
  printf("  Registered %s %s (ID %d, %s, year %d).\n", s.first, s.last, s.id,
         s.programme, s.year);
}

void display_students(Record *list) {
  int i;

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  printf("\n  --- Registered students (%d) ---\n", list->count);
  print_student_table_header();
  for (i = 0; i < list->count; i++)
    print_student_line(&list->items[i]);
}

void view_student(Record *list) {
  int id, index, i;

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  if (!ask_int("  Student ID: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(list, id);
  if (index < 0) {
    printf("  No student with ID %d.\n", id);
    return;
  }

  printf("\n  --- Student detail ---\n");
  printf("  ID        : %d\n", list->items[index].id);
  printf("  Name      : %s %s\n", list->items[index].first,
         list->items[index].last);
  printf("  Programme : %s\n", list->items[index].programme);
  printf("  Year      : %d\n", list->items[index].year);
  printf("  Courses   : %d\n", list->items[index].course_count);
  if (list->items[index].course_count > 0) {
    printf("  %-10s %-34s %6s %6s\n", "Code", "Course", "Mark", "Max");
    for (i = 0; i < list->items[index].course_count; i++)
      printf("  %-10s %-34s %6d %6d\n", list->items[index].courses[i].code,
             list->items[index].courses[i].name,
             list->items[index].courses[i].marks,
             list->items[index].courses[i].max_marks);
  }
  printf("  Average   : %.2f%%\n", student_average(&list->items[index]));
}

void update_student(Record *list) {
  int id, index, value;

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  if (!ask_int("  Student ID to update: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(list, id);
  if (index < 0) {
    printf("  No student with ID %d - nothing updated.\n", id);
    return;
  }

  printf("  Updating %s %s (leave a field empty to keep it)\n",
         list->items[index].first, list->items[index].last);
  printf("  First name [%s]: ", list->items[index].first);
  {
    char buffer[NAME_LEN];
    int status = read_line(buffer, NAME_LEN);
    if (status == 0)
      return;
    if (status == 1 && buffer[0] != '\0')
      strcpy(list->items[index].first, buffer);
  }
  printf("  Last name [%s]: ", list->items[index].last);
  {
    char buffer[NAME_LEN];
    int status = read_line(buffer, NAME_LEN);
    if (status == 0)
      return;
    if (status == 1 && buffer[0] != '\0')
      strcpy(list->items[index].last, buffer);
  }
  printf("  Programme [%s]: ", list->items[index].programme);
  {
    char buffer[PROG_LEN];
    int status = read_line(buffer, PROG_LEN);
    if (status == 0)
      return;
    if (status == 1 && buffer[0] != '\0')
      strcpy(list->items[index].programme, buffer);
  }
  printf("  Year [%d]: ", list->items[index].year);
  {
    char buffer[16];
    int status = read_line(buffer, 16);
    if (status == 0)
      return;
    if (status == 1 && buffer[0] != '\0') {
      value = atoi(buffer);
      if (value >= 1 && value <= 6)
        list->items[index].year = value;
      else
        printf("  Invalid year - the old value was kept.\n");
    }
  }
  printf("  Student record updated.\n");
}

void delete_student(Record *list) {
  int id, index, i;

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  if (!ask_int("  Student ID to delete: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(list, id);
  if (index < 0) {
    printf("  No student with ID %d - nothing deleted.\n", id);
    return;
  }

  printf("  Deleting %s %s (ID %d).\n", list->items[index].first,
         list->items[index].last, list->items[index].id);
  for (i = index; i + 1 < list->count; i++)
    list->items[i] = list->items[i + 1];
  list->count--;
  shrink_if_sparse(list);
  printf("  Deleted. Students now: %d (capacity %d).\n", list->count,
         list->capacity);
}

void add_course(Record *list) {
  int id, index, value;
  Course c;

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  if (!ask_int("  Student ID: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(list, id);
  if (index < 0) {
    printf("  No student with ID %d.\n", id);
    return;
  }
  if (list->items[index].course_count >= MAX_COURSES) {
    printf("  Rejected: %s %s already has the maximum of %d courses.\n",
           list->items[index].first, list->items[index].last, MAX_COURSES);
    return;
  }

  if (!ask_text("  Course code: ", c.code, CODE_LEN) ||
      !ask_text("  Course name: ", c.name, COURSE_NAME_LEN)) {
    printf("\n  Input closed.\n");
    return;
  }

  for (value = 0; value < list->items[index].course_count; value++) {
    if (compare_insensitive(list->items[index].courses[value].code, c.code) ==
        0) {
      printf("  Rejected: course %s is already registered for this student.\n",
             c.code);
      return;
    }
  }

  if (!ask_int("  Maximum marks (1-1000): ", 1, 1000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: maximum marks must be between 1 and 1000.\n",
               &value)) {
    printf("\n  Input closed.\n");
    return;
  }
  c.max_marks = value;
  if (!ask_int("  Marks obtained: ", 0, c.max_marks,
               "  Invalid: enter a whole number.\n",
               "  Invalid: marks must be between 0 and the maximum.\n",
               &value)) {
    printf("\n  Input closed.\n");
    return;
  }
  c.marks = value;

  list->items[index].courses[list->items[index].course_count] = c;
  list->items[index].course_count++;
  printf("  Added %s (%s) to %s %s.\n", c.code, c.name,
         list->items[index].first, list->items[index].last);
}

void update_marks(Record *list) {
  int id, index, i, value;
  char code[CODE_LEN];

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  if (!ask_int("  Student ID: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(list, id);
  if (index < 0) {
    printf("  No student with ID %d.\n", id);
    return;
  }
  if (list->items[index].course_count == 0) {
    printf("  This student has no courses to update.\n");
    return;
  }
  if (!ask_text("  Course code: ", code, CODE_LEN)) {
    printf("\n  Input closed.\n");
    return;
  }

  for (i = 0; i < list->items[index].course_count; i++) {
    if (compare_insensitive(list->items[index].courses[i].code, code) == 0) {
      printf("  Current marks: %d / %d\n", list->items[index].courses[i].marks,
             list->items[index].courses[i].max_marks);
      if (!ask_int("  New marks: ", 0, list->items[index].courses[i].max_marks,
                   "  Invalid: enter a whole number.\n",
                   "  Invalid: marks must be between 0 and the maximum.\n",
                   &value)) {
        printf("\n  Input closed.\n");
        return;
      }
      list->items[index].courses[i].marks = value;
      printf("  Marks updated to %d / %d.\n", value,
             list->items[index].courses[i].max_marks);
      return;
    }
  }
  printf("  Course %s is not registered for this student.\n", code);
}

void display_performance(Record *list) {
  int id, index, i;

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  if (!ask_int("  Student ID: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(list, id);
  if (index < 0) {
    printf("  No student with ID %d.\n", id);
    return;
  }
  if (list->items[index].course_count == 0) {
    printf("  %s %s has no courses, so no average can be calculated.\n",
           list->items[index].first, list->items[index].last);
    return;
  }

  printf("\n  --- Performance for %s %s (ID %d) ---\n",
         list->items[index].first, list->items[index].last,
         list->items[index].id);
  printf("  %-10s %-34s %8s %8s\n", "Code", "Course", "Mark", "Percent");
  for (i = 0; i < list->items[index].course_count; i++) {
    Course *c = &list->items[index].courses[i];
    printf("  %-10s %-34s %4d/%-3d %7.1f%%\n", c->code, c->name, c->marks,
           c->max_marks, 100.0 * c->marks / c->max_marks);
  }
  printf("\n  Overall average : %.2f%%\n",
         student_average(&list->items[index]));
  printf("  Courses passed  : %d of %d\n", courses_passed(&list->items[index]),
         list->items[index].course_count);
  printf("  Status          : %s\n",
         student_average(&list->items[index]) >= PASS_PERCENT ? "PASS"
                                                              : "FAIL");
}

// ---------------- manual search ----------------

void search_students(Record *list) {
  int mode, id, i, found = 0;
  char text[PROG_LEN];

  if (list->count == 0) {
    printf("\n  No students registered yet.\n");
    return;
  }
  printf("\n  Search by:\n    1. Student ID\n    2. First or last name\n");
  printf("    3. Programme of study\n");
  if (!ask_int("  Choice: ", 1, 3, "  Invalid: enter 1, 2 or 3.\n",
               "  Invalid: enter 1, 2 or 3.\n", &mode)) {
    printf("\n  Input closed.\n");
    return;
  }

  if (mode == 1) {
    if (!ask_int("  Student ID: ", 1, 2000000000,
                 "  Invalid: enter a whole number.\n",
                 "  Invalid: the ID must be positive.\n", &id)) {
      printf("\n  Input closed.\n");
      return;
    }
    printf("\n  Search results:\n");
    print_student_table_header();
    for (i = 0; i < list->count; i++)
      if (list->items[i].id == id) {
        print_student_line(&list->items[i]);
        found = 1;
      }
  } else if (mode == 2) {
    if (!ask_text("  Name to search for: ", text, PROG_LEN)) {
      printf("\n  Input closed.\n");
      return;
    }
    printf("\n  Search results:\n");
    print_student_table_header();
    for (i = 0; i < list->count; i++)
      if (contains_insensitive(list->items[i].first, text) ||
          contains_insensitive(list->items[i].last, text)) {
        print_student_line(&list->items[i]);
        found = 1;
      }
  } else {
    if (!ask_text("  Programme to search for: ", text, PROG_LEN)) {
      printf("\n  Input closed.\n");
      return;
    }
    printf("\n  Search results:\n");
    print_student_table_header();
    for (i = 0; i < list->count; i++)
      if (contains_insensitive(list->items[i].programme, text)) {
        print_student_line(&list->items[i]);
        found = 1;
      }
  }

  if (!found)
    printf("  No student matched that search.\n");
}

// ---------------- manual sorting (no qsort) ----------------

void swap_students(Student *a, Student *b) {
  Student temp = *a;
  *a = *b;
  *b = temp;
}

// bubble sort driven by a mode: 1 = ID, 2 = name, 3 = average, 4 = programme
void sort_students(Record *list) {
  int mode, i, j, order;

  if (list->count < 2) {
    printf("\n  Nothing to sort (fewer than two students).\n");
    return;
  }
  printf("\n  Sort by:\n    1. Student ID\n    2. Name (alphabetical)\n");
  printf("    3. Overall average (highest first)\n    4. Programme of study\n");
  if (!ask_int("  Choice: ", 1, 4, "  Invalid: enter 1, 2, 3 or 4.\n",
               "  Invalid: enter 1, 2, 3 or 4.\n", &mode)) {
    printf("\n  Input closed - sort cancelled.\n");
    return;
  }

  // the comparison is written out here so no library sort is used
  for (i = 0; i < list->count - 1; i++) {
    for (j = 0; j < list->count - 1 - i; j++) {
      order = 0;
      if (mode == 1) {
        order = list->items[j].id > list->items[j + 1].id;
      } else if (mode == 2) {
        int by_last =
            compare_insensitive(list->items[j].last, list->items[j + 1].last);
        int by_first =
            compare_insensitive(list->items[j].first, list->items[j + 1].first);
        order = by_last > 0 || (by_last == 0 && by_first > 0);
      } else if (mode == 3) {
        order = student_average(&list->items[j]) <
                student_average(&list->items[j + 1]);
      } else {
        order = compare_insensitive(list->items[j].programme,
                                    list->items[j + 1].programme) > 0;
      }
      if (order)
        swap_students(&list->items[j], &list->items[j + 1]);
    }
  }

  printf("  Sorted.\n");
  display_students(list);
}

// ---------------- academic report ----------------

void academic_report(Record *list) {
  int i, j;
  int total_courses = 0, passed_students = 0, failed_students = 0;
  int total_marks = 0, total_max = 0;
  int best = 0, worst = 0;
  char programme_names[16][PROG_LEN];
  int programme_students[16];
  int programme_count = 0;
  int found;

  if (list->count == 0) {
    printf("\n  No students registered yet - nothing to report.\n");
    return;
  }

  for (i = 0; i < list->count; i++) {
    total_courses += list->items[i].course_count;
    for (j = 0; j < list->items[i].course_count; j++) {
      total_marks += list->items[i].courses[j].marks;
      total_max += list->items[i].courses[j].max_marks;
    }
    if (student_average(&list->items[i]) >= PASS_PERCENT)
      passed_students++;
    else
      failed_students++;

    if (student_average(&list->items[i]) > student_average(&list->items[best]))
      best = i;
    if (student_average(&list->items[i]) < student_average(&list->items[worst]))
      worst = i;

    found = 0;
    for (j = 0; j < programme_count; j++)
      if (compare_insensitive(programme_names[j], list->items[i].programme) ==
          0) {
        programme_students[j]++;
        found = 1;
        break;
      }
    if (!found && programme_count < 16) {
      strcpy(programme_names[programme_count], list->items[i].programme);
      programme_students[programme_count] = 1;
      programme_count++;
    }
  }

  printf("\n  ============== ACADEMIC PERFORMANCE REPORT ==============\n");
  printf("  Registered students      : %d\n", list->count);
  printf("  Courses registered       : %d\n", total_courses);
  if (total_max > 0)
    printf("  Overall average          : %.2f%%\n",
           100.0 * total_marks / total_max);
  else
    printf("  Overall average          : not available (no courses yet)\n");
  printf("  Highest average student  : %s %s (%.2f%%)\n",
         list->items[best].first, list->items[best].last,
         student_average(&list->items[best]));
  printf("  Lowest average student   : %s %s (%.2f%%)\n",
         list->items[worst].first, list->items[worst].last,
         student_average(&list->items[worst]));
  printf("  Passing (average >= %.0f%%) : %d\n", PASS_PERCENT, passed_students);
  printf("  Failing                  : %d\n", failed_students);

  printf("\n  Average performance per course:\n");
  if (total_courses == 0) {
    printf("    No courses registered yet.\n");
  } else {
    printf("    %-10s %-34s %6s %8s\n", "Code", "Course", "Taken", "Average");
    for (i = 0; i < list->count; i++) {
      for (j = 0; j < list->items[i].course_count; j++) {
        int k, taken = 0, course_marks = 0, course_max = 0;
        char code[CODE_LEN];

        strcpy(code, list->items[i].courses[j].code);

        // only report a course once, at the point where it first appears
        for (k = 0; k < i; k++) {
          int n;
          for (n = 0; n < list->items[k].course_count; n++)
            if (compare_insensitive(list->items[k].courses[n].code, code) == 0)
              goto next_course;
        }

        for (k = 0; k < list->count; k++) {
          int n;
          for (n = 0; n < list->items[k].course_count; n++)
            if (compare_insensitive(list->items[k].courses[n].code, code) ==
                0) {
              taken++;
              course_marks += list->items[k].courses[n].marks;
              course_max += list->items[k].courses[n].max_marks;
            }
        }
        printf("    %-10s %-34s %6d %7.1f%%\n", code,
               list->items[i].courses[j].name, taken,
               course_max > 0 ? 100.0 * course_marks / course_max : 0.0);
      next_course:;
      }
    }
  }

  printf("\n  Students per programme of study:\n");
  for (i = 0; i < programme_count; i++)
    printf("    %-30s %d\n", programme_names[i], programme_students[i]);
  printf("  ========================================================\n");
}

// ---------------- file handling ----------------

// returns 1 when the file was written, 0 on failure
int save_records_to_file(Record *list) {
  FILE *file;
  int i, j;

  file = fopen(DATA_FILE, "w");
  if (file == NULL) {
    printf("  Error: could not open \"%s\" for writing.\n", DATA_FILE);
    return 0;
  }

  fprintf(file, "%d\n", list->count);
  for (i = 0; i < list->count; i++) {
    fprintf(file, "%d|%s|%s|%s|%d|%d\n", list->items[i].id,
            list->items[i].first, list->items[i].last, list->items[i].programme,
            list->items[i].year, list->items[i].course_count);
    for (j = 0; j < list->items[i].course_count; j++)
      fprintf(file, "%s|%s|%d|%d\n", list->items[i].courses[j].code,
              list->items[i].courses[j].name, list->items[i].courses[j].marks,
              list->items[i].courses[j].max_marks);
  }

  if (fclose(file) != 0) {
    printf("  Error: the file could not be closed properly.\n");
    return 0;
  }
  printf("  Saved %d student(s) to \"%s\".\n", list->count, DATA_FILE);
  return 1;
}

// returns 1 when records were loaded, 0 otherwise
int load_records_from_file(Record *list) {
  FILE *file;
  char line[256];
  int count, i, j, course_count;
  Student s;

  file = fopen(DATA_FILE, "r");
  if (file == NULL) {
    printf("  No saved file found at \"%s\" - starting empty.\n", DATA_FILE);
    return 0;
  }

  if (fgets(line, sizeof line, file) == NULL ||
      sscanf(line, "%d", &count) != 1) {
    printf("  Error: \"%s\" is not in the expected format.\n", DATA_FILE);
    fclose(file);
    return 0;
  }

  list->count = 0;
  for (i = 0; i < count; i++) {
    if (fgets(line, sizeof line, file) == NULL) {
      printf("  Error: the file ended unexpectedly after %d student(s).\n", i);
      fclose(file);
      return 0;
    }
    if (sscanf(line, "%d|%31[^|]|%31[^|]|%39[^|]|%d|%d", &s.id, s.first, s.last,
               s.programme, &s.year, &course_count) != 6) {
      printf("  Error: student record %d is malformed.\n", i + 1);
      fclose(file);
      return 0;
    }
    if (course_count < 0 || course_count > MAX_COURSES) {
      printf("  Error: student %d has an invalid course count.\n", s.id);
      fclose(file);
      return 0;
    }
    s.course_count = course_count;

    for (j = 0; j < course_count; j++) {
      if (fgets(line, sizeof line, file) == NULL ||
          sscanf(line, "%11[^|]|%47[^|]|%d|%d", s.courses[j].code,
                 s.courses[j].name, &s.courses[j].marks,
                 &s.courses[j].max_marks) != 4) {
        printf("  Error: course %d of student %d is malformed.\n", j + 1, s.id);
        fclose(file);
        return 0;
      }
    }

    if (!ensure_capacity(list, list->count + 1)) {
      fclose(file);
      return 0;
    }
    list->items[list->count++] = s;
  }

  fclose(file);
  printf("  Loaded %d student(s) from \"%s\".\n", list->count, DATA_FILE);
  return 1;
}

// menu wrappers: the dispatch table needs every operation to return void
void save_records(Record *list) { (void)save_records_to_file(list); }

void load_records(Record *list) { (void)load_records_from_file(list); }

void quit_program(Record *list) {
  char answer[8];
  int status;

  (void)list;
  printf("\n  Save before exiting? [y/N]: ");
  status = read_line(answer, sizeof answer);
  if (status == 1 && (answer[0] == 'y' || answer[0] == 'Y')) {
    save_records(list);
  }
  g_running = 0;
  printf("  Exiting.\n");
}

// ---------------- dispatch ----------------

Operation operations[] = {
    register_student, display_students, view_student,    update_student,
    delete_student,   add_course,       update_marks,    display_performance,
    search_students,  sort_students,    academic_report, save_records,
    load_records,     quit_program};

const char *menu_labels[] = {"Register a new student",
                             "Display all students",
                             "View a student's details",
                             "Update student information",
                             "Delete a student record",
                             "Add a course to a student",
                             "Update marks for a course",
                             "Display a student's performance",
                             "Search students (ID / name / programme)",
                             "Sort students (ID / name / average / programme)",
                             "Generate the academic report",
                             "Save records to file",
                             "Load records from file",
                             "Exit"};

int MENU_COUNT = sizeof(operations) / sizeof(operations[0]);

void print_header(void) {
  printf(
      "\n  ==================================================================\n"
      "  Student ID : %s\n"
      "  Project    : Student Academic Records and Performance Analytics\n"
      "  Storage    : %s\n"
      "  ==================================================================\n",
      STUDENT_ID, DATA_FILE);
}

void print_menu(void) {
  int i;

  printf("\n  ------------------------- MENU -------------------------\n");
  for (i = 0; i < MENU_COUNT; i++)
    printf("  %2d. %s\n", i + 1, menu_labels[i]);
  printf("  --------------------------------------------------------\n");
}

// ---------------- starting data ----------------

Student seed_students[] = {{1001,
                            "Aline",
                            "Uwase",
                            "Computer Science",
                            2,
                            3,
                            {{"CS101", "C Programming", 78, 100},
                             {"CS102", "Data Structures", 65, 100},
                             {"MA101", "Discrete Mathematics", 54, 100}}},
                           {1002,
                            "Kevin",
                            "Niyonzima",
                            "Information Technology",
                            1,
                            2,
                            {{"CS101", "C Programming", 45, 100},
                             {"IT101", "Web Technologies", 62, 100}}},
                           {1003,
                            "Diane",
                            "Mukamana",
                            "Computer Science",
                            3,
                            3,
                            {{"CS102", "Data Structures", 88, 100},
                             {"CS201", "Operating Systems", 91, 100},
                             {"MA102", "Linear Algebra", 76, 100}}}};

int SEED_COUNT = sizeof(seed_students) / sizeof(seed_students[0]);

void load_seed_students(Record *list) {
  int i;

  for (i = 0; i < SEED_COUNT; i++) {
    if (!ensure_capacity(list, list->count + 1))
      return;
    list->items[list->count++] = seed_students[i];
  }
}

int main() {
  Record list;
  int status, choice;

  list.capacity = INITIAL_CAP;
  list.count = 0;
  list.items = calloc(INITIAL_CAP, sizeof(Student));
  if (list.items == NULL) {
    printf("Error: could not allocate memory for the student records.\n");
    return 1;
  }
  printf("  [mem] records 0 -> %d slots\n", INITIAL_CAP);

  load_seed_students(&list);
  print_header();
  printf("\n  Loaded %d sample student(s) (capacity %d).\n", list.count,
         list.capacity);

  while (g_running) {
    print_menu();
    status = read_int("  Enter choice: ", &choice);
    if (status == 0) {
      printf("\n  Input closed.\n");
      break;
    }
    if (status == -1 || choice < 1 || choice > MENU_COUNT) {
      printf("  Invalid: enter a number from 1 to %d.\n", MENU_COUNT);
      continue;
    }
    operations[choice - 1](&list);
  }

  free(list.items);
  list.items = NULL;
  list.count = 0;
  list.capacity = 0;
  printf("\n  All memory released. Goodbye.\n");
  return 0;
}
