#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

DEVICE="${1:-sda}"
INTERFACE="${2:-eth0}"
SERVICE="xyz.openbmc_project.Control.SyncBMCData.service"
RESULTS_DIR="/tmp/perf_results_$(date +%Y%m%d_%H%M%S)"

mkdir -p "$RESULTS_DIR"

echo "Results: $RESULTS_DIR"

#####################################
# Get PIDs once (IMPORTANT OPTIMIZATION)
#####################################
get_pids() {
  RSYNC_PIDS=$(pidof rsync 2>/dev/null)
  STUNNEL_PIDS=$(pidof stunnel 2>/dev/null)

  DATASYNC_PIDS=""
  for pid in /proc/[0-9]*; do
    [ -f "$pid/cmdline" ] || continue
    cmd=$(tr '\0' ' ' < "$pid/cmdline" 2>/dev/null)
    case "$cmd" in
      *SyncBMCData*|*data-sync*|*phosphor-rbmc-data-sync*)
        DATASYNC_PIDS="$DATASYNC_PIDS $(basename $pid)"
        ;;
    esac
  done
}

#####################################
# CPU + IOWAIT (correct delta method)
#####################################
start_cpu_monitor() {
  while true; do
    ts=$(date +%s)

    read user nice system idle iowait irq softirq steal < /proc/stat

    total=$((user + nice + system + idle + iowait + irq + softirq + steal))

    echo "$ts,$total,$iowait" >> "$RESULTS_DIR/iowait.log"

    sleep 1
  done &
  echo $!
}

#####################################
# Process CPU (lightweight)
#####################################
get_proc_cpu() {
  pids="$1"
  sum=0

  for pid in $pids; do
    [ -f "/proc/$pid/stat" ] || continue
    read _ _ _ _ _ _ _ _ _ _ _ _ _ utime stime _ < /proc/$pid/stat
    sum=$((sum + utime + stime))
  done

  echo $sum
}

start_proc_cpu_monitor() {
  get_pids

  while true; do
    ts=$(date +%s)

    RSYNC_CPU=$(get_proc_cpu "$RSYNC_PIDS")
    STUNNEL_CPU=$(get_proc_cpu "$STUNNEL_PIDS")
    DATASYNC_CPU=$(get_proc_cpu "$DATASYNC_PIDS")

    echo "$ts,$RSYNC_CPU,$STUNNEL_CPU,$DATASYNC_CPU" >> "$RESULTS_DIR/cpu.log"

    sleep 1
  done &
  echo $!
}

#####################################
# Disk (simplified)
#####################################
start_disk_monitor() {
  while true; do
    ts=$(date +%s)
    val=$(cat /sys/block/$DEVICE/stat 2>/dev/null | awk '{print $10}')
    echo "$ts,$val" >> "$RESULTS_DIR/disk.log"
    sleep 1
  done &
  echo $!
}

#####################################
# Network
#####################################
start_net_monitor() {
  while true; do
    ts=$(date +%s)
    awk -v ts="$ts" -v iface="$INTERFACE:" '
      $0 ~ iface {print ts","$2","$10}
    ' /proc/net/dev >> "$RESULTS_DIR/network.log"
    sleep 1
  done &
  echo $!
}

#####################################
# MEMORY
#####################################
start_mem_monitor() {
  while true; do
    ts=$(date +%s)
    awk -v ts="$ts" '/MemAvailable/ {print ts","$2}' /proc/meminfo >> "$RESULTS_DIR/memory.log"
    sleep 1
  done &
  echo $!
}

#####################################
# START MONITORS
#####################################
CPU_PID=$(start_proc_cpu_monitor)
DISK_PID=$(start_disk_monitor)
NET_PID=$(start_net_monitor)
IO_PID=$(start_cpu_monitor)
MEM_PID=$(start_mem_monitor)

echo "Monitors started"

#####################################
# RUN TEST
#####################################
START=$(date '+%Y-%m-%d %H:%M:%S')
systemctl restart "$SERVICE"

echo "Waiting for sync..."

while ! journalctl -u "$SERVICE" --since "$START" 2>/dev/null | grep -q "Full Sync completed"; do
  sleep 2
done

echo "Sync done"

#####################################
# STOP MONITORS
#####################################
kill $CPU_PID $DISK_PID $NET_PID $IO_PID $MEM_PID 2>/dev/null

#####################################
# NOTE
#####################################
echo "Logs saved in $RESULTS_DIR"