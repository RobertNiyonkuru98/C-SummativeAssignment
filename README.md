# C Programming - Summative Assignment

**Robert Tony Mitali Niyonkuru** - student ID 100

Five programs written in C11 (plus one Arduino sketch) for the summative
assignment. Each project lives in its own folder with the source at the top and
a `docs/` folder holding the write-up and the captured evidence.

## Contents

| Folder | Program | What it demonstrates |
|---|---|---|
| `P1-OccupancyMonitor/` | `occupancymonitor.ino` for Arduino Uno | structs, two dynamic arrays grown with `realloc`, debouncing with `millis()`, a 16x2 LCD and Serial reporting |
| `P2-SystemHealth/` | `systemhealth.sh` | bash functions, `case`, loops, reading `/proc`, a timestamped activity log |
| `P3-AcademicRecords/` | `academicrecords.c` | array of structs, pointers, `malloc`/`realloc`/`free`, sorting and searching, file persistence |
| `P4-SalesTransactions/` | `salestransactions.c` | function-pointer dispatch table, structures, aggregation, save and load to file |
| `P5-DocAnalyzer/` | `docanalyzer.c` | POSIX threads (`pthread`), one thread per file, shared-memory safety with `malloc`ed per-thread arguments, per-file reports |

## Requirements

- **P1** - Arduino IDE (any recent version) or Tinkercad Circuits, and the
  `LiquidCrystal` library that ships with the IDE.
- **P2** - bash 4 or newer on Linux (written and tested in WSL/Ubuntu).
- **P3, P4, P5** - a C11 compiler. Every command below was checked with
  `gcc 13` under WSL/Ubuntu with `-Wall -Wextra -std=c11 -pedantic`, and the
  code compiles with **no warnings**.

## Build and run

There is no Makefile by design: each program is a single translation unit, so
one command builds it. Run the commands from inside the project folder.

### P1 - Smart Occupancy Monitoring and Access Control

Open `P1-OccupancyMonitor/occupancymonitor.ino` in the Arduino IDE, select
**Arduino Uno** and upload. The same sketch runs in Tinkercad Circuits on an
Uno with a 16x2 LCD, two pushbuttons, a green LED, a red LED and a 250 k
potentiometer for contrast.

The circuit, its bill of materials and the schematic are pictured in
`docs/PoC-2026-11-25.md`, section 8.

To check the sketch compiles without opening the IDE:

```bash
arduino-cli compile --fqbn arduino:avr:uno P1-OccupancyMonitor/occupancymonitor.ino
```

### P2 - System Health Monitor

```bash
cd P2-SystemHealth
chmod +x systemhealth.sh
./systemhealth.sh
```

### P3 - Student Academic Records

```bash
cd P3-AcademicRecords
gcc -Wall -Wextra -std=c11 -pedantic academicrecords.c -o academicrecords
./academicrecords
```

### P4 - Sales Transactions

```bash
cd P4-SalesTransactions
gcc -Wall -Wextra -std=c11 -pedantic salestransactions.c -o salestransactions
./salestransactions
```

### P5 - Document Keyword Analyzer

```bash
cd P5-DocAnalyzer
gcc -pthread -Wall -Wextra -std=c11 -pedantic docanalyzer.c -o docanalyzer
./docanalyzer "IoT" samples/document1.txt samples/document2.txt samples/document3.txt
```

`docanalyzer` takes the keyword first and then one or more files. It starts a
thread per file, writes `<file>_report.txt` next to each input and prints a
summary when the threads join. The three sample documents are in `samples/`.

## Documentation

Every project has a write-up at `docs/PoC-<date>.md` covering the design, the
data structures, the memory management, the error handling and a captured run.
The `docs/evidence/` folder next to it holds what those write-ups refer to:

- `compile.log` - the compiler output proving a clean build
- `sanitizer_run.txt` - a full run under `-fsanitize=address,undefined`, with no
  diagnostics and no leaks
- `outputs/sample_run.txt` - a complete recorded session, prompts and answers
- project-specific proofs, such as the dynamic array growth log in P3, the
  threading summary in P5, and the Tinkercad screenshots in P1

## How the programs were checked

Beyond compiling without warnings, three things were used throughout:

**Memory checking.** P3, P4 and P5 were each run under AddressSanitizer and
UndefinedBehaviorSanitizer:

```bash
gcc -fsanitize=address,undefined -g -std=c11 program.c -o program_asan
./program_asan
```

**Logic testing without hardware.** The Arduino sketch cannot run on a PC, so
`P1-OccupancyMonitor/docs/evidence/occupancy_sim_test.cpp` drives the sketch's
own functions with simulated button presses and asserts the results - 32 checks
covering entry, exit, the capacity limit, the empty-room limit and the display
strings. Build it with:

```bash
cd P1-OccupancyMonitor
g++ -Wall -std=c++17 -I docs/evidence docs/evidence/occupancy_sim_test.cpp -o test
./test
```

`docs/evidence/lcd_preview.cpp` goes further and renders the 16x2 display and
the LED states on the terminal, so the screen can be seen without the board.

**Shell checking.** `P2-SystemHealth/systemhealth.sh` passes `bash -n` and
`shellcheck` with no warnings; the log is in `docs/evidence/shellcheck.log`.

## Notes

- Input is read line by line and the programs never use `gets`; long input is
  truncated rather than overflowing a buffer.
- Every allocation is paired with a `free`, and P5 frees each thread's argument
  after joining it.
- The three programs that persist data write to their own text file in the
  project folder and reload it on the next run; those files are created at
  runtime, not stored in the repository.
