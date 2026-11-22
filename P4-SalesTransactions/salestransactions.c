#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STUDENT_ID "100"
#define PRODUCT_LEN 40
#define DATE_LEN 12
#define INITIAL_CAP 4
#define DATA_FILE "sales_transactions.txt"

typedef struct {
  int id;
  char product[PRODUCT_LEN];
  int quantity;
  double unit_price;
  double total;        // quantity * unit_price, kept in step on every change
  char date[DATE_LEN]; // YYYY-MM-DD
} Transaction;

typedef struct {
  Transaction *items;
  int count;
  int capacity;
} Ledger;

// one menu operation
typedef void (*LedgerOp)(Ledger *);

// a processing operation used by "Process Sales Data"
typedef void (*SalesProc)(const Ledger *);

// a callback: decides whether one transaction matches, using a context pointer
typedef int (*FilterFn)(const Transaction *, const void *);

// a compare callback used by the hand-written sort
typedef int (*CompareFn)(const Transaction *, const Transaction *);

int g_running = 1;

// ---------------- input helpers ----------------

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

int read_double(const char *prompt, double *out) {
  char buf[32], *end;
  int status;

  printf("%s", prompt);
  status = read_line(buf, 32);
  if (status != 1)
    return status == 0 ? 0 : -1;
  *out = strtod(buf, &end);
  while (*end == ' ' || *end == '\t')
    end++;
  return (end == buf || *end != '\0') ? -1 : 1;
}

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

int ask_double(const char *prompt, double low, double high,
               const char *not_number, const char *out_of_range, double *out) {
  int status;

  for (;;) {
    status = read_double(prompt, out);
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

int compare_insensitive(const char *a, const char *b) {
  char la[PRODUCT_LEN], lb[PRODUCT_LEN];
  int i;

  for (i = 0; i + 1 < PRODUCT_LEN && a[i]; i++)
    la[i] = (char)tolower((unsigned char)a[i]);
  la[i] = '\0';
  for (i = 0; i + 1 < PRODUCT_LEN && b[i]; i++)
    lb[i] = (char)tolower((unsigned char)b[i]);
  lb[i] = '\0';
  return strcmp(la, lb);
}

int contains_insensitive(const char *haystack, const char *needle) {
  char lo_hay[PRODUCT_LEN], lo_need[PRODUCT_LEN];
  size_t i;

  for (i = 0; i + 1 < PRODUCT_LEN && haystack[i]; i++)
    lo_hay[i] = (char)tolower((unsigned char)haystack[i]);
  lo_hay[i] = '\0';
  for (i = 0; i + 1 < PRODUCT_LEN && needle[i]; i++)
    lo_need[i] = (char)tolower((unsigned char)needle[i]);
  lo_need[i] = '\0';
  return strstr(lo_hay, lo_need) != NULL;
}

// ---------------- ledger and memory helpers ----------------

int ensure_capacity(Ledger *ledger, int needed) {
  int new_cap = ledger->capacity;
  Transaction *grown;

  if (needed <= ledger->capacity)
    return 1;
  while (new_cap < needed)
    new_cap *= 2;
  grown = realloc(ledger->items, new_cap * sizeof(Transaction));
  if (grown == NULL) {
    printf(
        "  [mem] could not grow the transaction list - operation cancelled.\n");
    return 0;
  }
  printf("  [mem] transactions %d -> %d slots\n", ledger->capacity, new_cap);
  ledger->items = grown;
  ledger->capacity = new_cap;
  return 1;
}

int shrink_if_sparse(Ledger *ledger) {
  int new_cap;
  Transaction *shrunk;

  if (ledger->capacity <= INITIAL_CAP || ledger->count > ledger->capacity / 4)
    return 1;
  new_cap =
      ledger->capacity / 2 < INITIAL_CAP ? INITIAL_CAP : ledger->capacity / 2;
  shrunk = realloc(ledger->items, new_cap * sizeof(Transaction));
  if (shrunk == NULL)
    return 1;
  printf("  [mem] transactions %d -> %d slots (shrunk)\n", ledger->capacity,
         new_cap);
  ledger->items = shrunk;
  ledger->capacity = new_cap;
  return 1;
}

int find_index_by_id(const Ledger *ledger, int id) {
  int i;

  for (i = 0; i < ledger->count; i++)
    if (ledger->items[i].id == id)
      return i;
  return -1;
}

// the total is derived from the two stored values, never typed by the user
void set_total(Transaction *t) { t->total = t->quantity * t->unit_price; }

void today(char *out, int size) {
  time_t now = time(NULL);
  struct tm *local = localtime(&now);

  if (local == NULL) {
    strcpy(out, "unknown");
    return;
  }
  strftime(out, size, "%Y-%m-%d", local);
}

void print_transaction(const Transaction *t) {
  printf("  %-5d %-28s %5d %12.2f %14.2f  %s\n", t->id, t->product, t->quantity,
         t->unit_price, t->total, t->date);
}

void print_table_header(void) {
  printf("  %-5s %-28s %5s %12s %14s  %s\n", "ID", "Product", "Qty", "Unit",
         "Total", "Date");
  printf("  %-5s %-28s %5s %12s %14s  %s\n", "-----",
         "----------------------------", "-----", "------------",
         "--------------", "----------");
}

// ---------------- manual sorting with compare callbacks ----------------
// bubble sort written out by hand - qsort is not used anywhere in this program

void swap_transactions(Transaction *a, Transaction *b) {
  Transaction temp = *a;
  *a = *b;
  *b = temp;
}

void sort_with(Ledger *ledger, CompareFn compare) {
  int i, j;

  for (i = 0; i < ledger->count - 1; i++)
    for (j = 0; j < ledger->count - 1 - i; j++)
      if (compare(&ledger->items[j], &ledger->items[j + 1]) > 0)
        swap_transactions(&ledger->items[j], &ledger->items[j + 1]);
}

int compare_id_asc(const Transaction *a, const Transaction *b) {
  return a->id - b->id;
}

int compare_product_asc(const Transaction *a, const Transaction *b) {
  int by_product = compare_insensitive(a->product, b->product);
  return by_product != 0 ? by_product : a->id - b->id;
}

int compare_total_asc(const Transaction *a, const Transaction *b) {
  if (a->total < b->total)
    return -1;
  if (a->total > b->total)
    return 1;
  return a->id - b->id;
}

int compare_total_desc(const Transaction *a, const Transaction *b) {
  return -compare_total_asc(a, b);
}

// ---------------- sales processing (function pointers) ----------------

void proc_total_revenue(const Ledger *ledger) {
  int i;
  double revenue = 0;

  for (i = 0; i < ledger->count; i++)
    revenue += ledger->items[i].total;
  printf("  Total revenue           : %.2f\n", revenue);
}

void proc_average_value(const Ledger *ledger) {
  int i;
  double revenue = 0;

  for (i = 0; i < ledger->count; i++)
    revenue += ledger->items[i].total;
  if (ledger->count == 0) {
    printf("  Average transaction     : not available (no transactions)\n");
    return;
  }
  printf("  Average transaction     : %.2f\n", revenue / ledger->count);
}

void proc_highest(const Ledger *ledger) {
  int i, best = 0;

  if (ledger->count == 0) {
    printf("  Highest transaction     : none\n");
    return;
  }
  for (i = 1; i < ledger->count; i++)
    if (ledger->items[i].total > ledger->items[best].total)
      best = i;
  printf("  Highest transaction     : #%d %s (%.2f)\n", ledger->items[best].id,
         ledger->items[best].product, ledger->items[best].total);
}

void proc_lowest(const Ledger *ledger) {
  int i, worst = 0;

  if (ledger->count == 0) {
    printf("  Lowest transaction      : none\n");
    return;
  }
  for (i = 1; i < ledger->count; i++)
    if (ledger->items[i].total < ledger->items[worst].total)
      worst = i;
  printf("  Lowest transaction      : #%d %s (%.2f)\n", ledger->items[worst].id,
         ledger->items[worst].product, ledger->items[worst].total);
}

void proc_total_units(const Ledger *ledger) {
  int i, units = 0;

  for (i = 0; i < ledger->count; i++)
    units += ledger->items[i].quantity;
  printf("  Total units sold        : %d\n", units);
}

// the table of processing operations, called through SalesProc pointers
SalesProc sales_operations[] = {proc_total_revenue, proc_average_value,
                                proc_highest, proc_lowest, proc_total_units};

const char *sales_labels[] = {"Total revenue", "Average transaction value",
                              "Highest-value transaction",
                              "Lowest-value transaction", "Total units sold"};

int SALES_COUNT = sizeof(sales_operations) / sizeof(sales_operations[0]);

// ---------------- callbacks ----------------

// matches transactions whose total is at least the amount pointed at
int filter_above_amount(const Transaction *t, const void *context) {
  double limit = *(const double *)context;
  return t->total >= limit;
}

// matches transactions whose product contains the text pointed at
int filter_by_product(const Transaction *t, const void *context) {
  const char *product = (const char *)context;
  return contains_insensitive(t->product, product) != 0;
}

// matches transactions that sold at least the quantity pointed at
int filter_min_quantity(const Transaction *t, const void *context) {
  int minimum = *(const int *)context;
  return t->quantity >= minimum;
}

// runs the callback over the ledger and prints every match
int apply_filter(const Ledger *ledger, FilterFn matches, const void *context) {
  int i, found = 0;

  print_table_header();
  for (i = 0; i < ledger->count; i++) {
    if (matches(&ledger->items[i], context)) {
      print_transaction(&ledger->items[i]);
      found++;
    }
  }
  return found;
}

// ---------------- menu: transactions ----------------

void add_transaction(Ledger *ledger) {
  Transaction t;
  int value;
  double price;

  printf("\n  --- Add a new transaction ---\n");
  if (!ensure_capacity(ledger, ledger->count + 1))
    return;

  for (;;) {
    if (!ask_int("  Transaction ID: ", 1, 2000000000,
                 "  Invalid: enter a whole number.\n",
                 "  Invalid: the ID must be positive.\n", &value)) {
      printf("\n  Input closed - cancelled.\n");
      return;
    }
    if (find_index_by_id(ledger, value) >= 0) {
      printf("  Rejected: transaction ID %d already exists.\n", value);
      continue;
    }
    t.id = value;
    break;
  }

  if (!ask_text("  Product name: ", t.product, PRODUCT_LEN)) {
    printf("\n  Input closed - cancelled.\n");
    return;
  }
  if (!ask_int("  Quantity (1-10000): ", 1, 10000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the quantity must be between 1 and 10000.\n",
               &value)) {
    printf("\n  Input closed - cancelled.\n");
    return;
  }
  t.quantity = value;
  if (!ask_double("  Unit price (0.01-10000000): ", 0.01, 10000000.0,
                  "  Invalid: enter a number.\n",
                  "  Invalid: the price must be greater than zero.\n",
                  &price)) {
    printf("\n  Input closed - cancelled.\n");
    return;
  }
  t.unit_price = price;
  set_total(&t);
  today(t.date, DATE_LEN);

  ledger->items[ledger->count++] = t;
  printf("  Added transaction #%d: %s, %d x %.2f = %.2f (dated %s).\n", t.id,
         t.product, t.quantity, t.unit_price, t.total, t.date);
}

void display_transactions(Ledger *ledger) {
  int i;

  if (ledger->count == 0) {
    printf("\n  No transactions recorded.\n");
    return;
  }
  printf("\n  --- Transactions (%d) ---\n", ledger->count);
  print_table_header();
  for (i = 0; i < ledger->count; i++)
    print_transaction(&ledger->items[i]);
}

void update_transaction(Ledger *ledger) {
  int id, index, value;
  double price;

  if (ledger->count == 0) {
    printf("\n  No transactions recorded.\n");
    return;
  }
  if (!ask_int("  Transaction ID to update: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(ledger, id);
  if (index < 0) {
    printf("  No transaction with ID %d - nothing updated.\n", id);
    return;
  }

  printf("  Updating #%d (leave a field empty to keep the current value)\n",
         ledger->items[index].id);
  {
    char buffer[PRODUCT_LEN];
    int status;
    printf("  Product [%s]: ", ledger->items[index].product);
    status = read_line(buffer, PRODUCT_LEN);
    if (status == 0)
      return;
    if (status == 1 && buffer[0] != '\0')
      strcpy(ledger->items[index].product, buffer);
  }
  {
    char buffer[32];
    int status;
    printf("  Quantity [%d]: ", ledger->items[index].quantity);
    status = read_line(buffer, 32);
    if (status == 0)
      return;
    if (status == 1 && buffer[0] != '\0') {
      value = atoi(buffer);
      if (value >= 1 && value <= 10000)
        ledger->items[index].quantity = value;
      else
        printf("  Invalid quantity - the old value was kept.\n");
    }
  }
  {
    char buffer[32];
    int status;
    printf("  Unit price [%.2f]: ", ledger->items[index].unit_price);
    status = read_line(buffer, 32);
    if (status == 0)
      return;
    if (status == 1 && buffer[0] != '\0') {
      price = strtod(buffer, NULL);
      if (price > 0)
        ledger->items[index].unit_price = price;
      else
        printf("  Invalid price - the old value was kept.\n");
    }
  }

  set_total(&ledger->items[index]);
  printf("  Updated. New total: %.2f\n", ledger->items[index].total);
}

void delete_transaction(Ledger *ledger) {
  int id, index, i;

  if (ledger->count == 0) {
    printf("\n  No transactions recorded.\n");
    return;
  }
  if (!ask_int("  Transaction ID to delete: ", 1, 2000000000,
               "  Invalid: enter a whole number.\n",
               "  Invalid: the ID must be positive.\n", &id)) {
    printf("\n  Input closed.\n");
    return;
  }
  index = find_index_by_id(ledger, id);
  if (index < 0) {
    printf("  No transaction with ID %d - nothing deleted.\n", id);
    return;
  }

  printf("  Deleting #%d (%s, %.2f).\n", ledger->items[index].id,
         ledger->items[index].product, ledger->items[index].total);
  for (i = index; i + 1 < ledger->count; i++)
    ledger->items[i] = ledger->items[i + 1];
  ledger->count--;
  shrink_if_sparse(ledger);
  printf("  Deleted. Transactions now: %d (capacity %d).\n", ledger->count,
         ledger->capacity);
}

// ---------------- menu: processing, filter, search, sort, report
// ----------------

void process_sales(Ledger *ledger) {
  int choice, status, i;

  if (ledger->count == 0) {
    printf("\n  No transactions recorded - nothing to process.\n");
    return;
  }
  printf("\n  Sales processing operations:\n");
  for (i = 0; i < SALES_COUNT; i++)
    printf("    %d. %s\n", i + 1, sales_labels[i]);

  status = read_int("  Choice: ", &choice);
  if (status == 0) {
    printf("\n  Input closed.\n");
    return;
  }
  if (status == -1 || choice < 1 || choice > SALES_COUNT) {
    printf("  Invalid choice.\n");
    return;
  }

  printf("\n");
  // the operation is reached through a function pointer taken from the table
  sales_operations[choice - 1](ledger);
}

void filter_transactions(Ledger *ledger) {
  int choice, status, found, minimum;
  double amount;
  char product[PRODUCT_LEN];

  if (ledger->count == 0) {
    printf("\n  No transactions recorded.\n");
    return;
  }
  printf("\n  Filter by callbacks:\n");
  printf("    1. Total amount at least a given value\n");
  printf("    2. Product name contains text\n");
  printf("    3. Quantity at least a given value\n");

  status = read_int("  Choice: ", &choice);
  if (status == 0) {
    printf("\n  Input closed.\n");
    return;
  }
  if (status == -1 || choice < 1 || choice > 3) {
    printf("  Invalid choice.\n");
    return;
  }

  printf("\n");
  if (choice == 1) {
    if (!ask_double("  Minimum total amount: ", 0.0, 1000000000.0,
                    "  Invalid: enter a number.\n",
                    "  Invalid: the amount must not be negative.\n", &amount)) {
      printf("\n  Input closed.\n");
      return;
    }
    found = apply_filter(ledger, filter_above_amount, &amount);
  } else if (choice == 2) {
    if (!ask_text("  Product text: ", product, PRODUCT_LEN)) {
      printf("\n  Input closed.\n");
      return;
    }
    found = apply_filter(ledger, filter_by_product, product);
  } else {
    if (!ask_int("  Minimum quantity: ", 1, 10000,
                 "  Invalid: enter a whole number.\n",
                 "  Invalid: the quantity must be at least 1.\n", &minimum)) {
      printf("\n  Input closed.\n");
      return;
    }
    found = apply_filter(ledger, filter_min_quantity, &minimum);
  }

  if (found == 0)
    printf("  No transaction matched that filter.\n");
}

void search_transactions(Ledger *ledger) {
  int mode, id, i, found = 0;
  double amount;
  char product[PRODUCT_LEN];

  if (ledger->count == 0) {
    printf("\n  No transactions recorded.\n");
    return;
  }
  printf("\n  Search by:\n    1. Transaction ID\n    2. Product name\n");
  printf("    3. Exact total amount\n");
  if (!ask_int("  Choice: ", 1, 3, "  Invalid: enter 1, 2 or 3.\n",
               "  Invalid: enter 1, 2 or 3.\n", &mode)) {
    printf("\n  Input closed.\n");
    return;
  }

  printf("\n");
  if (mode == 1) {
    if (!ask_int("  Transaction ID: ", 1, 2000000000,
                 "  Invalid: enter a whole number.\n",
                 "  Invalid: the ID must be positive.\n", &id)) {
      printf("\n  Input closed.\n");
      return;
    }
    printf("  Results:\n");
    print_table_header();
    for (i = 0; i < ledger->count; i++)
      if (ledger->items[i].id == id) {
        print_transaction(&ledger->items[i]);
        found = 1;
      }
  } else if (mode == 2) {
    if (!ask_text("  Product text: ", product, PRODUCT_LEN)) {
      printf("\n  Input closed.\n");
      return;
    }
    printf("  Results:\n");
    print_table_header();
    for (i = 0; i < ledger->count; i++)
      if (contains_insensitive(ledger->items[i].product, product)) {
        print_transaction(&ledger->items[i]);
        found = 1;
      }
  } else {
    if (!ask_double("  Total amount: ", 0.0, 1000000000.0,
                    "  Invalid: enter a number.\n",
                    "  Invalid: the amount must not be negative.\n", &amount)) {
      printf("\n  Input closed.\n");
      return;
    }
    printf("  Results:\n");
    print_table_header();
    for (i = 0; i < ledger->count; i++)
      if (ledger->items[i].total > amount - 0.005 &&
          ledger->items[i].total < amount + 0.005) {
        print_transaction(&ledger->items[i]);
        found = 1;
      }
  }

  if (!found)
    printf("  No transaction matched that search.\n");
}

void sort_transactions(Ledger *ledger) {
  int choice;

  if (ledger->count < 2) {
    printf("\n  Nothing to sort.\n");
    return;
  }
  printf("\n  Sort by:\n    1. Transaction ID\n    2. Product name\n");
  printf("    3. Total amount (ascending)\n    4. Total amount (descending)\n");
  if (!ask_int("  Choice: ", 1, 4, "  Invalid: enter 1, 2, 3 or 4.\n",
               "  Invalid: enter 1, 2, 3 or 4.\n", &choice)) {
    printf("\n  Input closed.\n");
    return;
  }

  if (choice == 1)
    sort_with(ledger, compare_id_asc);
  else if (choice == 2)
    sort_with(ledger, compare_product_asc);
  else if (choice == 3)
    sort_with(ledger, compare_total_asc);
  else
    sort_with(ledger, compare_total_desc);

  printf("  Sorted.\n");
  display_transactions(ledger);
}

void generate_report(Ledger *ledger) {
  int i, j, units = 0, product_count = 0;
  double revenue = 0, product_revenue[32];
  char product_names[32][PRODUCT_LEN];
  int found;

  if (ledger->count == 0) {
    printf("\n  No transactions recorded - nothing to report.\n");
    return;
  }

  for (i = 0; i < ledger->count; i++) {
    revenue += ledger->items[i].total;
    units += ledger->items[i].quantity;

    found = 0;
    for (j = 0; j < product_count; j++)
      if (compare_insensitive(product_names[j], ledger->items[i].product) ==
          0) {
        product_revenue[j] += ledger->items[i].total;
        found = 1;
        break;
      }
    if (!found && product_count < 32) {
      strcpy(product_names[product_count], ledger->items[i].product);
      product_revenue[product_count] = ledger->items[i].total;
      product_count++;
    }
  }

  printf("\n  ============== SALES REPORT ==============\n");
  printf("  Transactions recorded  : %d\n", ledger->count);
  printf("  Total revenue          : %.2f\n", revenue);
  printf("  Average transaction    : %.2f\n", revenue / ledger->count);
  printf("  Total units sold       : %d\n", units);
  {
    int best = 0, worst = 0;
    for (i = 1; i < ledger->count; i++) {
      if (ledger->items[i].total > ledger->items[best].total)
        best = i;
      if (ledger->items[i].total < ledger->items[worst].total)
        worst = i;
    }
    printf("  Highest transaction    : #%d %s (%.2f)\n", ledger->items[best].id,
           ledger->items[best].product, ledger->items[best].total);
    printf("  Lowest transaction     : #%d %s (%.2f)\n",
           ledger->items[worst].id, ledger->items[worst].product,
           ledger->items[worst].total);
  }
  printf("\n  Revenue by product:\n");
  for (j = 0; j < product_count; j++)
    printf("    %-28s %12.2f\n", product_names[j], product_revenue[j]);
  printf("  =========================================\n");
}

// ---------------- file handling ----------------

int save_to_file(Ledger *ledger) {
  FILE *file;
  int i;

  file = fopen(DATA_FILE, "w");
  if (file == NULL) {
    printf("  Error: could not open \"%s\" for writing.\n", DATA_FILE);
    return 0;
  }
  fprintf(file, "%d\n", ledger->count);
  for (i = 0; i < ledger->count; i++)
    fprintf(file, "%d|%s|%d|%.2f|%s\n", ledger->items[i].id,
            ledger->items[i].product, ledger->items[i].quantity,
            ledger->items[i].unit_price, ledger->items[i].date);
  if (fclose(file) != 0) {
    printf("  Error: the file could not be closed properly.\n");
    return 0;
  }
  printf("  Saved %d transaction(s) to \"%s\".\n", ledger->count, DATA_FILE);
  return 1;
}

int load_from_file(Ledger *ledger) {
  FILE *file;
  char line[256];
  int count, i;
  Transaction t;

  file = fopen(DATA_FILE, "r");
  if (file == NULL) {
    printf("  No saved file found at \"%s\".\n", DATA_FILE);
    return 0;
  }
  if (fgets(line, sizeof line, file) == NULL ||
      sscanf(line, "%d", &count) != 1) {
    printf("  Error: \"%s\" is not in the expected format.\n", DATA_FILE);
    fclose(file);
    return 0;
  }

  ledger->count = 0;
  for (i = 0; i < count; i++) {
    if (fgets(line, sizeof line, file) == NULL ||
        sscanf(line, "%d|%39[^|]|%d|%lf|%11[^|\n]", &t.id, t.product,
               &t.quantity, &t.unit_price, t.date) != 5) {
      printf("  Error: transaction %d in the file is malformed.\n", i + 1);
      fclose(file);
      return 0;
    }
    set_total(&t);
    if (!ensure_capacity(ledger, ledger->count + 1)) {
      fclose(file);
      return 0;
    }
    ledger->items[ledger->count++] = t;
  }

  fclose(file);
  printf("  Loaded %d transaction(s) from \"%s\".\n", ledger->count, DATA_FILE);
  return 1;
}

// menu wrappers so every entry in the dispatch table returns void
void save_transactions(Ledger *ledger) { (void)save_to_file(ledger); }
void load_transactions(Ledger *ledger) { (void)load_from_file(ledger); }

void quit_program(Ledger *ledger) {
  char answer[8];
  int status;

  printf("\n  Save before exiting? [y/N]: ");
  status = read_line(answer, sizeof answer);
  if (status == 1 && (answer[0] == 'y' || answer[0] == 'Y'))
    (void)save_to_file(ledger);
  g_running = 0;
  printf("  Exiting.\n");
}

// ---------------- dispatch table ----------------

LedgerOp operations[] = {
    add_transaction,     display_transactions, update_transaction,
    delete_transaction,  process_sales,        filter_transactions,
    search_transactions, sort_transactions,    generate_report,
    save_transactions,   load_transactions,    quit_program};

const char *menu_labels[] = {"Add a new transaction",
                             "Display all transactions",
                             "Update a transaction",
                             "Delete a transaction",
                             "Process sales data (function pointers)",
                             "Filter transactions (callbacks)",
                             "Search transactions",
                             "Sort transactions",
                             "Generate the sales report",
                             "Save transactions to file",
                             "Load transactions from file",
                             "Exit"};

int MENU_COUNT = sizeof(operations) / sizeof(operations[0]);

void print_header(void) {
  printf(
      "\n  ==================================================================\n"
      "  Student ID : %s\n"
      "  Project    : Sales Transaction Processing and Analysis System\n"
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

Transaction seed_transactions[] = {
    {1001, "Laptop Pro 14", 2, 850000.00, 1700000.00, "2026-11-20"},
    {1002, "Wireless Mouse", 15, 18000.00, 270000.00, "2026-11-21"},
    {1003, "USB-C Hub", 7, 42000.00, 294000.00, "2026-11-22"},
    {1004, "Monitor 27in", 3, 320000.00, 960000.00, "2026-11-23"}};

int SEED_COUNT = sizeof(seed_transactions) / sizeof(seed_transactions[0]);

void load_seed_transactions(Ledger *ledger) {
  int i;

  for (i = 0; i < SEED_COUNT; i++) {
    if (!ensure_capacity(ledger, ledger->count + 1))
      return;
    ledger->items[ledger->count++] = seed_transactions[i];
  }
}

int main() {
  Ledger ledger;
  int status, choice;

  ledger.capacity = INITIAL_CAP;
  ledger.count = 0;
  ledger.items = calloc(INITIAL_CAP, sizeof(Transaction));
  if (ledger.items == NULL) {
    printf("Error: could not allocate memory for the transactions.\n");
    return 1;
  }
  printf("  [mem] transactions 0 -> %d slots\n", INITIAL_CAP);

  load_seed_transactions(&ledger);
  print_header();
  printf("\n  Loaded %d sample transaction(s) (capacity %d).\n", ledger.count,
         ledger.capacity);

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
    operations[choice - 1](&ledger);
  }

  free(ledger.items);
  ledger.items = NULL;
  ledger.count = 0;
  ledger.capacity = 0;
  printf("\n  All memory released. Goodbye.\n");
  return 0;
}
