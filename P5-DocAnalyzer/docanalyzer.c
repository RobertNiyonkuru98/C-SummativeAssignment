#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STUDENT_ID "100"
#define PATH_LEN 256
#define LINE_LEN 1024
#define KEYWORD_LEN 64
#define INITIAL_MATCHES 16

typedef struct {
  int thread_index;           // 1-based, used in every message
  char input_path[PATH_LEN];  // the file this thread analyses
  char output_path[PATH_LEN]; // where its report is written
  char keyword[KEYWORD_LEN];  // shared copy of the search keyword

  int total_lines;    // lines read from the input file
  int occurrences;    // keyword hits across the whole file
  int matching_lines; // lines containing at least one hit
  int *line_numbers;  // numbers of the matching lines (heap)
  int line_capacity;

  int failed;        // 1 when the file could not be processed
  int started;       // 1 once its thread has actually been created
  char message[320]; // reason shown in the summary
} ThreadArg;

// ---------------- small helpers ----------------

void copy_lower(char *out, const char *in, int size) {
  int i;

  for (i = 0; i + 1 < size && in[i] != '\0'; i++)
    out[i] = (char)tolower((unsigned char)in[i]);
  out[i] = '\0';
}

// count how many times `needle` appears inside `haystack` (already lowercased)
int count_occurrences(const char *haystack, const char *needle) {
  const char *found = haystack;
  int count = 0;
  size_t length = strlen(needle);

  if (length == 0)
    return 0;
  while ((found = strstr(found, needle)) != NULL) {
    count++;
    found += length;
  }
  return count;
}

// remember the number of a matching line, growing the array as needed
int remember_line(ThreadArg *arg, int line_number) {
  int *grown;
  int new_capacity;

  if (arg->matching_lines < arg->line_capacity) {
    arg->line_numbers[arg->matching_lines] = line_number;
    return 1;
  }

  new_capacity = arg->line_capacity * 2;
  grown = realloc(arg->line_numbers, new_capacity * sizeof(int));
  if (grown == NULL) {
    printf("  [Thread %d] out of memory remembering line numbers\n",
           arg->thread_index);
    return 0;
  }
  arg->line_numbers = grown;
  arg->line_capacity = new_capacity;
  arg->line_numbers[arg->matching_lines] = line_number;
  return 1;
}

// build "<name>_report.txt" next to the input file
void build_report_path(const char *input, char *output, int size) {
  int last_dot = -1;
  int i;

  for (i = 0; input[i] != '\0' && i + 1 < size; i++)
    if (input[i] == '.')
      last_dot = i;

  if (last_dot > 0) {
    int copy = last_dot;
    if (copy > size - 12)
      copy = size - 12;
    memcpy(output, input, copy);
    output[copy] = '\0';
  } else {
    strcpy(output, input);
  }
  strcat(output, "_report.txt");
}

// ---------------- report ----------------

int generate_report(ThreadArg *arg) {
  FILE *out = fopen(arg->output_path, "w");
  int i;

  if (out == NULL) {
    snprintf(arg->message, sizeof arg->message,
             "could not create the report file %s", arg->output_path);
    return 0;
  }

  fprintf(out, "==============================================\n");
  fprintf(out, "  DOCUMENT ANALYSIS REPORT\n");
  fprintf(out, "==============================================\n");
  fprintf(out, "  Input file      : %s\n", arg->input_path);
  fprintf(out, "  Search keyword  : %s\n", arg->keyword);
  fprintf(out, "  Total lines     : %d\n", arg->total_lines);
  fprintf(out, "  Occurrences     : %d\n", arg->occurrences);
  fprintf(out, "  Matching lines  : %d\n", arg->matching_lines);
  fprintf(out, "  Status          : %s\n", arg->failed ? "FAILED" : "SUCCESS");

  fprintf(out, "  Line numbers    : ");
  if (arg->matching_lines == 0) {
    fprintf(out, "none (the keyword does not appear in this file)\n");
  } else {
    for (i = 0; i < arg->matching_lines; i++)
      fprintf(out, "%d%s", arg->line_numbers[i],
              i + 1 < arg->matching_lines ? ", " : "\n");
  }
  fprintf(out, "==============================================\n");

  if (fclose(out) != 0) {
    snprintf(arg->message, sizeof arg->message,
             "the report file could not be closed");
    return 0;
  }
  return 1;
}

// ---------------- the thread itself ----------------

void *process_file(void *data) {
  ThreadArg *arg = (ThreadArg *)data;
  FILE *input;
  char line[LINE_LEN];
  char lower_line[LINE_LEN];
  char lower_keyword[KEYWORD_LEN];
  int line_number = 0;

  // the keyword is kept as typed for the report, and lowercased only for
  // matching
  copy_lower(lower_keyword, arg->keyword, KEYWORD_LEN);

  printf("  [Thread %d] Processing %s ...\n", arg->thread_index,
         arg->input_path);

  input = fopen(arg->input_path, "r");
  if (input == NULL) {
    snprintf(arg->message, sizeof arg->message, "could not open %s",
             arg->input_path);
    arg->failed = 1;
    printf("  [Thread %d] ERROR: could not open %s\n", arg->thread_index,
           arg->input_path);
    return NULL;
  }

  while (fgets(line, sizeof line, input) != NULL) {
    int hits;

    line_number++;
    copy_lower(lower_line, line, LINE_LEN);
    hits = count_occurrences(lower_line, lower_keyword);

    if (hits > 0) {
      arg->occurrences += hits;
      if (!remember_line(arg, line_number)) {
        arg->failed = 1;
        snprintf(arg->message, sizeof arg->message,
                 "out of memory while recording matches");
        fclose(input);
        return NULL;
      }
      arg->matching_lines++;
    }
  }

  arg->total_lines = line_number;
  if (ferror(input)) {
    snprintf(arg->message, sizeof arg->message, "error while reading %s",
             arg->input_path);
    arg->failed = 1;
    fclose(input);
    printf("  [Thread %d] ERROR while reading %s\n", arg->thread_index,
           arg->input_path);
    return NULL;
  }
  fclose(input);

  if (arg->total_lines == 0) {
    snprintf(arg->message, sizeof arg->message, "the file is empty");
    arg->failed = 1;
    printf("  [Thread %d] %s is empty\n", arg->thread_index, arg->input_path);
    return NULL;
  }

  if (!generate_report(arg)) {
    arg->failed = 1;
    printf("  [Thread %d] ERROR: %s\n", arg->thread_index, arg->message);
    return NULL;
  }

  printf("  [Thread %d] Successfully processed %s (%d line(s), %d "
         "occurrence(s))\n",
         arg->thread_index, arg->input_path, arg->total_lines,
         arg->occurrences);
  return NULL;
}

// ---------------- summary ----------------

void print_summary(ThreadArg *args, int count) {
  int i;
  int completed = 0, failed = 0;

  printf("\n  ===== PROCESSING SUMMARY =====\n\n");
  for (i = 0; i < count; i++) {
    if (args[i].failed) {
      printf("  %-30s FAILED (%s)\n", args[i].input_path, args[i].message);
      failed++;
    } else {
      printf("  %-30s Completed (%d lines, %d matches)\n", args[i].input_path,
             args[i].total_lines, args[i].matching_lines);
      completed++;
    }
  }
  printf("\n  Files completed : %d\n", completed);
  if (failed > 0)
    printf("  Files failed    : %d\n", failed);
  printf("\n  Reports written :\n");
  for (i = 0; i < count; i++)
    if (!args[i].failed)
      printf("    %s\n", args[i].output_path);
}

// ---------------- main ----------------

int main(int argc, char *argv[]) {
  ThreadArg *args;
  pthread_t *threads;
  int file_count, i, created = 0;

  printf("\n  ================== MULTI-THREADED DOCUMENT ANALYZER "
         "==================\n");
  printf("  Student ID : %s\n", STUDENT_ID);

  if (argc < 3) {
    printf("\n  Usage: %s \"keyword\" file1 [file2 ...]\n", argv[0]);
    printf("  Example: %s \"IoT\" document1.txt document2.txt\n", argv[0]);
    return 1;
  }

  file_count = argc - 2;
  printf("  Search keyword   : %s\n", argv[1]);
  printf("  Input files      : %d\n", file_count);
  printf("  "
         "====================================================================="
         "=\n");

  args = calloc(file_count, sizeof(ThreadArg));
  threads = calloc(file_count, sizeof(pthread_t));
  if (args == NULL || threads == NULL) {
    printf("\n  Error: could not allocate memory for the thread arguments.\n");
    free(args);
    free(threads);
    return 1;
  }

  // one argument block per file, filled in before any thread starts
  for (i = 0; i < file_count; i++) {
    args[i].thread_index = i + 1;
    snprintf(args[i].input_path, PATH_LEN, "%s", argv[i + 2]);
    build_report_path(args[i].input_path, args[i].output_path, PATH_LEN);
    snprintf(args[i].keyword, KEYWORD_LEN, "%s", argv[1]);

    args[i].total_lines = 0;
    args[i].occurrences = 0;
    args[i].matching_lines = 0;
    args[i].line_capacity = INITIAL_MATCHES;
    args[i].line_numbers = malloc(INITIAL_MATCHES * sizeof(int));
    args[i].failed = 0;
    args[i].started = 0;
    args[i].message[0] = '\0';

    if (args[i].line_numbers == NULL) {
      printf("  Error: could not allocate the line-number buffer.\n");
      args[i].failed = 1;
      snprintf(args[i].message, sizeof args[i].message, "out of memory");
    }
  }

  // one thread per file
  for (i = 0; i < file_count; i++) {
    if (args[i].failed)
      continue;
    printf("  [Main] Creating thread %d for %s\n", args[i].thread_index,
           args[i].input_path);
    if (pthread_create(&threads[i], NULL, process_file, &args[i]) != 0) {
      args[i].failed = 1;
      snprintf(args[i].message, sizeof args[i].message,
               "thread could not be created");
      printf("  [Main] ERROR: could not create thread %d\n",
             args[i].thread_index);
      continue;
    }
    args[i].started = 1;
    created++;
  }

  printf("  [Main] %d thread(s) started, waiting for them to finish...\n",
         created);

  // join only the threads that were actually created
  for (i = 0; i < file_count; i++) {
    if (!args[i].started)
      continue;
    pthread_join(threads[i], NULL);
  }

  print_summary(args, file_count);

  for (i = 0; i < file_count; i++)
    free(args[i].line_numbers);
  free(args);
  free(threads);

  printf("\n  All processing tasks completed. Memory released.\n");
  return 0;
}
