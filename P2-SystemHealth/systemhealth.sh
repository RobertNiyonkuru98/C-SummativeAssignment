#!/bin/bash
LOG_DIR="$HOME/.syshealth"
LOG_FILE="$LOG_DIR/activity.log"
REPORT_DIR="$LOG_DIR/reports"

CPU_THRESHOLD=80
MEM_THRESHOLD=80
DISK_THRESHOLD=80

log_activity() {
  local message="$1"
  mkdir -p "$LOG_DIR" 2>/dev/null
  printf "[%s] %s\n" "$(date '+%Y-%m-%d %H:%M:%S')" "$message" >>"$LOG_FILE" 2>/dev/null
}

heading() {
  echo
  echo "=============================================================="
  echo "  $1"
  echo "=============================================================="
}

confirm() {
  local answer
  printf "%s [y/N]: " "$1"
  read -r answer
  case "$answer" in
    y | Y | yes | YES) return 0 ;;
    *) return 1 ;;
  esac
}

THRESHOLD_RESULT=""

read_threshold() {
  local label="$1"
  local current="$2"
  local value

  while true; do
    printf "Set %s warning threshold (1-100) [%s]: " "$label" "$current"
    read -r value

    if [ -z "$value" ]; then
      THRESHOLD_RESULT="$current"
      return 0
    fi

    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
      echo "  Invalid: enter a whole number between 1 and 100."
      continue
    fi
    if [ "$value" -lt 1 ] || [ "$value" -gt 100 ]; then
      echo "  Invalid: the threshold must be between 1 and 100."
      continue
    fi
    THRESHOLD_RESULT="$value"
    return 0
  done
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "  Error: the '$1' command is not available on this system."
    log_activity "WARNING: required command '$1' is missing"
    return 1
  fi
  return 0
}

show_system_info() {
  heading "SYSTEM INFORMATION"

  require_command hostname || return 1

  echo "  Hostname        : $(hostname)"
  echo "  Operating system: $(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME")"
  echo "  Kernel version  : $(uname -r)"
  echo "  Architecture    : $(uname -m)"
  echo "  Uptime          : $(uptime -p 2>/dev/null || uptime)"
  echo "  Current date    : $(date '+%A, %d %B %Y')"
  echo "  Current time    : $(date '+%H:%M:%S')"

  log_activity "Viewed system information"
}

read_cpu_usage() {
  local -a fields
  local total1 total2 idle1 idle2 delta_total delta_idle

  read -r -a fields < /proc/stat
  total1=$((fields[1] + fields[2] + fields[3] + fields[4] + fields[5] + fields[6] + fields[7] + fields[8]))
  idle1=$((fields[4] + fields[5]))

  sleep 1

  read -r -a fields < /proc/stat
  total2=$((fields[1] + fields[2] + fields[3] + fields[4] + fields[5] + fields[6] + fields[7] + fields[8]))
  idle2=$((fields[4] + fields[5]))

  delta_total=$((total2 - total1))
  delta_idle=$((idle2 - idle1))

  if [ "$delta_total" -eq 0 ]; then
    echo 0
    return 0
  fi
  echo $(((100 * (delta_total - delta_idle)) / delta_total))
}

monitor_cpu() {
  local usage

  heading "CPU UTILISATION"
  require_command ps || return 1

  usage=$(read_cpu_usage)
  echo "  CPU usage (1 second sample): ${usage}%"
  echo "  Cores available            : $(getconf _NPROCESSORS_ONLN 2>/dev/null || echo unknown)"

  read_threshold "CPU" "$CPU_THRESHOLD"
  CPU_THRESHOLD="$THRESHOLD_RESULT"
  echo "  CPU warning threshold is now ${CPU_THRESHOLD}%"

  if [ "$usage" -ge "$CPU_THRESHOLD" ]; then
    echo "  WARNING: CPU usage (${usage}%) is at or above the ${CPU_THRESHOLD}% threshold."
    log_activity "WARNING: CPU usage ${usage}% >= threshold ${CPU_THRESHOLD}%"
  else
    echo "  CPU usage is within the ${CPU_THRESHOLD}% threshold."
    log_activity "Monitored CPU: ${usage}% (threshold ${CPU_THRESHOLD}%)"
  fi

  echo
  echo "  Processes using the most CPU:"
  ps -eo pid,pcpu,pmem,comm --sort=-pcpu 2>/dev/null | head -n 6 |
    sed 's/^/    /'
}

monitor_memory() {
  local total available used percent

  heading "MEMORY UTILISATION"

  if [ ! -r /proc/meminfo ]; then
    echo "  Error: /proc/meminfo is not readable on this system."
    log_activity "ERROR: /proc/meminfo not readable"
    return 1
  fi

  total=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo)
  available=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)

  if [ -z "$total" ] || [ "$total" -eq 0 ]; then
    echo "  Error: could not read total memory."
    log_activity "ERROR: could not read total memory"
    return 1
  fi

  used=$((total - available))
  percent=$((100 * used / total))

  echo "  Total memory    : $((total / 1024)) MB"
  echo "  Used memory     : $((used / 1024)) MB"
  echo "  Available memory: $((available / 1024)) MB"
  echo "  Utilisation     : ${percent}%"

  read_threshold "memory" "$MEM_THRESHOLD"
  MEM_THRESHOLD="$THRESHOLD_RESULT"

  if [ "$percent" -ge "$MEM_THRESHOLD" ]; then
    echo "  WARNING: memory utilisation (${percent}%) is at or above the ${MEM_THRESHOLD}% threshold."
    log_activity "WARNING: memory utilisation ${percent}% >= threshold ${MEM_THRESHOLD}%"
  else
    echo "  Memory utilisation is within the ${MEM_THRESHOLD}% threshold."
    log_activity "Monitored memory: ${percent}% (threshold ${MEM_THRESHOLD}%)"
  fi
}

monitor_disk() {
  local fs size used avail pcent mount use over_count

  heading "DISK UTILISATION"
  require_command df || return 1

  read_threshold "disk" "$DISK_THRESHOLD"
  DISK_THRESHOLD="$THRESHOLD_RESULT"

  printf "  %-28s %10s %10s %10s %6s  %s\n" "Filesystem" "Size" "Used" "Available" "Use%" "Mounted on"
  echo "  -------------------------------------------------------------------------------"

  over_count=0
  while read -r fs size used avail pcent mount; do
    printf "  %-28s %10s %10s %10s %6s  %s\n" "$fs" "$size" "$used" "$avail" "$pcent" "$mount"
    use=${pcent%\%}
    if [ "$use" -ge "$DISK_THRESHOLD" ]; then
      over_count=$((over_count + 1))
    fi
  done < <(df -P -x tmpfs -x devtmpfs -x squashfs -x fuse 2>/dev/null | grep -v snapfuse | tail -n +2)

  echo
  echo "  Filesystems at or above the ${DISK_THRESHOLD}% threshold:"
  if [ "$over_count" -eq 0 ]; then
    echo "    None - all filesystems are below ${DISK_THRESHOLD}%."
    log_activity "Monitored disk: all filesystems below ${DISK_THRESHOLD}%"
    return 0
  fi

  while read -r fs size used avail pcent mount; do
    use=${pcent%\%}
    if [ "$use" -ge "$DISK_THRESHOLD" ]; then
      echo "    WARNING: $mount ($fs) is ${pcent} full"
    fi
  done < <(df -P -x tmpfs -x devtmpfs -x squashfs -x fuse 2>/dev/null | grep -v snapfuse | tail -n +2)

  log_activity "WARNING: ${over_count} filesystem(s) at or above ${DISK_THRESHOLD}%"
}

search_process() {
  local query pid

  heading "RUNNING PROCESSES"
  require_command ps || return 1

  echo "  10 processes using the most CPU:"
  ps -eo pid,user,pcpu,pmem,comm --sort=-pcpu 2>/dev/null | head -n 11 | sed 's/^/    /'

  printf "\n  Search by process name or PID (leave empty to skip): "
  read -r query

  if [ -z "$query" ]; then
    log_activity "Viewed running processes"
    return 0
  fi

  echo
  if [[ "$query" =~ ^[0-9]+$ ]]; then
    pid="$query"
    if ps -p "$pid" >/dev/null 2>&1; then
      echo "  Process with PID $pid:"
      ps -p "$pid" -o pid,user,pcpu,pmem,comm 2>/dev/null | sed 's/^/    /'
      log_activity "Searched process by PID $pid (found)"
    else
      echo "  No process is running with PID $pid."
      log_activity "Searched process by PID $pid (not found)"
    fi
  else
    echo "  Processes matching \"$query\":"
    if ps -eo pid,user,pcpu,pmem,comm 2>/dev/null | grep -i -- "$query" | grep -v grep | sed 's/^/    /' | grep -q .; then
      ps -eo pid,user,pcpu,pmem,comm 2>/dev/null | grep -i -- "$query" | grep -v grep | sed 's/^/    /'
      log_activity "Searched process by name \"$query\" (found)"
    else
      echo "    No matching processes."
      log_activity "Searched process by name \"$query\" (not found)"
    fi
  fi
}

terminate_process() {
  local pid name

  heading "TERMINATE A PROCESS"
  require_command ps || return 1
  require_command kill || return 1

  printf "  Enter the PID to terminate: "
  read -r pid

  if ! [[ "$pid" =~ ^[0-9]+$ ]]; then
    echo "  Invalid: the PID must be a whole number."
    log_activity "ERROR: invalid PID entered for termination"
    return 1
  fi

  if [ "$pid" -eq 1 ] || [ "$pid" -eq "$$" ] || [ "$pid" -eq "$PPID" ]; then
    echo "  Refused: $pid is a protected process (init or this monitoring session)."
    log_activity "REFUSED: attempt to terminate protected PID $pid"
    return 1
  fi

  if ! ps -p "$pid" >/dev/null 2>&1; then
    echo "  No process is running with PID $pid."
    log_activity "ERROR: tried to terminate missing PID $pid"
    return 1
  fi

  name=$(ps -p "$pid" -o comm= 2>/dev/null)
  echo "  Target: PID $pid ($name)"
  ps -p "$pid" -o pid,user,pcpu,pmem,comm 2>/dev/null | sed 's/^/    /'

  if ! confirm "  Terminate this process?"; then
    echo "  Cancelled - the process was not touched."
    log_activity "Cancelled termination of PID $pid ($name)"
    return 0
  fi

  if kill -TERM "$pid" 2>/dev/null; then
    echo "  Sent SIGTERM to PID $pid ($name)."
    log_activity "Terminated process PID $pid ($name)"
  else
    echo "  Failed: you may not have permission to terminate PID $pid."
    log_activity "ERROR: permission denied terminating PID $pid ($name)"
  fi
}

generate_report() {
  local dir file

  heading "SYSTEM HEALTH REPORT"

  printf "  Directory for the report [%s]: " "$REPORT_DIR"
  read -r dir
  [ -z "$dir" ] && dir="$REPORT_DIR"

  if [ ! -d "$dir" ]; then
    if ! mkdir -p "$dir" 2>/dev/null; then
      echo "  Error: could not create or access the directory \"$dir\"."
      log_activity "ERROR: report directory \"$dir\" is not accessible"
      return 1
    fi
    echo "  Created directory $dir"
  fi

  if [ ! -w "$dir" ]; then
    echo "  Error: no permission to write into \"$dir\"."
    log_activity "ERROR: no write permission for \"$dir\""
    return 1
  fi

  file="$dir/health-report-$(date '+%Y%m%d-%H%M%S').txt"

  {
    echo "=============================================================="
    echo "  SYSTEM HEALTH REPORT"
    echo "  Generated : $(date '+%Y-%m-%d %H:%M:%S')"
    echo "  Hostname  : $(hostname)"
    echo "  Kernel    : $(uname -r)"
    echo "  Uptime    : $(uptime -p 2>/dev/null || uptime)"
    echo "=============================================================="
    echo
    echo "CPU"
    echo "---"
    echo "  Usage     : ${CPU_THRESHOLD}% is the configured warning threshold"
    echo
    echo "MEMORY"
    echo "------"
    awk '/^MemTotal:|^MemFree:|^MemAvailable:/ {printf "  %-14s %s %s\n", $1, $2/1024, "MB"}' /proc/meminfo
    echo
    echo "DISK"
    echo "----"
    df -h -x tmpfs -x devtmpfs -x squashfs -x fuse 2>/dev/null | grep -v snapfuse | sed 's/^/  /'
    echo
    echo "TOP CPU PROCESSES"
    echo "-----------------"
    ps -eo pid,pcpu,pmem,comm --sort=-pcpu 2>/dev/null | head -n 6 | sed 's/^/  /'
  } >"$file" 2>/dev/null

  if [ -s "$file" ]; then
    echo
    echo "  Report written to: $file"
    echo "  Size: $(wc -l <"$file") lines"
    log_activity "Generated health report $file"
  else
    echo "  Error: the report could not be written."
    log_activity "ERROR: failed to write report $file"
    return 1
  fi
}

view_log() {
  heading "ACTIVITY HISTORY"

  if [ ! -f "$LOG_FILE" ]; then
    echo "  No activity has been recorded yet."
    return 0
  fi

  if [ ! -s "$LOG_FILE" ]; then
    echo "  The activity log is empty."
    return 0
  fi

  echo "  Log file: $LOG_FILE ($(wc -l <"$LOG_FILE") entries)"
  echo
  tail -n 40 "$LOG_FILE" | sed 's/^/  /'
}

clear_log() {
  heading "CLEAR ACTIVITY LOG"

  if [ ! -f "$LOG_FILE" ]; then
    echo "  There is no log file to clear."
    return 0
  fi

  if ! confirm "  Delete every entry in the activity log?"; then
    echo "  Cancelled - the log was not changed."
    return 0
  fi

  : >"$LOG_FILE"
  echo "  The activity log has been cleared."
  log_activity "Cleared the activity log"
}

show_menu() {
  echo
  echo "=============================================================="
  echo "   LINUX SYSTEM HEALTH AND RESOURCE MONITOR"
  echo "=============================================================="
  echo "   1) Display system information"
  echo "   2) Monitor CPU utilisation"
  echo "   3) Monitor memory utilisation"
  echo "   4) Monitor disk utilisation"
  echo "   5) View and search running processes"
  echo "   6) Terminate a process"
  echo "   7) Generate a system health report"
  echo "   8) View activity history"
  echo "   9) Clear the activity log"
  echo "  10) Exit"
  echo "=============================================================="
}

main() {
  local choice

  mkdir -p "$LOG_DIR" 2>/dev/null
  log_activity "Monitoring session started"

  while true; do
    show_menu
    printf "  Enter choice: "
    read -r choice

    case "$choice" in
      1) show_system_info ;;
      2) monitor_cpu ;;
      3) monitor_memory ;;
      4) monitor_disk ;;
      5) search_process ;;
      6) terminate_process ;;
      7) generate_report ;;
      8) view_log ;;
      9) clear_log ;;
      10)
        log_activity "Monitoring session ended"
        echo "  Exiting. The activity log is kept at $LOG_FILE"
        exit 0
        ;;
      "")
        echo "  Invalid: enter a number from 1 to 10."
        ;;
      *)
        echo "  Invalid: enter a number from 1 to 10."
        log_activity "ERROR: invalid menu choice \"$choice\""
        ;;
    esac
  done
}
main
