#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Performance test script for phosphor-data-sync
# Run this on the ACTIVE BMC to measure full sync performance
#
# Usage: ./perf_test.sh [device] [interface]
# Example: ./perf_test.sh sda eth2

# Configuration - adjust these for your system
DEVICE="${1:-sda}"          # Block device (sda, mmcblk0, nvme0n1)
INTERFACE="${2:-eth2}"      # Network interface (eth2, eth0, usb0)
SERVICE="xyz.openbmc_project.Control.SyncBMCData.service"
RESULTS_DIR="/tmp/perf_results_$(date +%Y%m%d_%H%M%S)"

echo "========================================="
echo "  Data Sync Performance Test"
echo "========================================="
echo ""
echo "Configuration:"
echo "  Device:    $DEVICE"
echo "  Interface: $INTERFACE"
echo "  Results:   $RESULTS_DIR"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"

# Helper function to get PIDs once
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
  echo "  [DEBUG] get_pids completed" >&2
}

# Helper function to get process CPU (lightweight)
get_proc_cpu() {
  pids="$1"
  sum=0

  for pid in $pids; do
    [ -f "/proc/$pid/stat" ] || continue
    read _ _ _ _ _ _ _ _ _ _ _ _ _ utime stime _ < /proc/$pid/stat 2>/dev/null || continue
    # Ensure utime and stime have values (default to 0 if empty)
    utime=${utime:-0}
    stime=${stime:-0}
    sum=$((sum + utime + stime))
  done

  echo $sum
}

# Function to start all monitors
start_monitors() {
  # Get PIDs once at start
  get_pids

  # CPU monitor - track service CPU usage (raw jiffies)
  while true; do
    TIMESTAMP=$(date +%s)

    RSYNC_CPU=$(get_proc_cpu "$RSYNC_PIDS")
    STUNNEL_CPU=$(get_proc_cpu "$STUNNEL_PIDS")
    DATASYNC_CPU=$(get_proc_cpu "$DATASYNC_PIDS")

    echo "$TIMESTAMP,$RSYNC_CPU,$STUNNEL_CPU,$DATASYNC_CPU" >> "$RESULTS_DIR/cpu.log"

    sleep 1
  done &
  CPU_PID=$!
  echo "  CPU monitor started (PID: $CPU_PID)"

  # Disk I/O monitor - track disk utilization
  while true; do
    TIMESTAMP=$(date +%s)
    IO_TICKS=$(awk '{print $13}' /sys/block/$DEVICE/stat 2>/dev/null || echo 0)
    echo "$TIMESTAMP,$IO_TICKS" >> "$RESULTS_DIR/disk.log"
    sleep 1
  done &
  DISK_PID=$!
  echo "  Disk I/O monitor started (PID: $DISK_PID)"

  # Network monitor - track RX/TX bandwidth
  while true; do
    TIMESTAMP=$(date +%s)
    cat /proc/net/dev | grep "$INTERFACE:" | \
      awk -v ts="$TIMESTAMP" '{print ts","$2","$10}' >> "$RESULTS_DIR/network.log"
    sleep 1
  done &
  NET_PID=$!
  echo "  Network monitor started (PID: $NET_PID)"

  # I/O wait monitor - track CPU I/O wait (raw values)
  while true; do
    TIMESTAMP=$(date +%s)
    # Read /proc/stat and extract CPU values using awk for robust parsing
    CPU_LINE=$(head -1 /proc/stat 2>/dev/null)
    if [ -n "$CPU_LINE" ]; then
      set -- $(echo "$CPU_LINE" | awk '{print $2, $3, $4, $5, $6, $7, $8, $9}')
      user=${1:-0}
      nice=${2:-0}
      system=${3:-0}
      idle=${4:-0}
      iowait=${5:-0}
      irq=${6:-0}
      softirq=${7:-0}
      steal=${8:-0}
      total=$((user + nice + system + idle + iowait + irq + softirq + steal))
    else
      total=0
      iowait=0
    fi
    echo "$TIMESTAMP,$total,$iowait" >> "$RESULTS_DIR/iowait.log"
    sleep 1
  done &
  IOWAIT_PID=$!
  echo "  I/O wait monitor started (PID: $IOWAIT_PID)"

  # Memory monitor - track available memory
  while true; do
    TIMESTAMP=$(date +%s)
    awk -v ts="$TIMESTAMP" '/MemAvailable/ {print ts","$2}' /proc/meminfo >> "$RESULTS_DIR/memory.log"
    sleep 1
  done &
  MEM_PID=$!
  echo "  Memory monitor started (PID: $MEM_PID)"

  echo "$CPU_PID $DISK_PID $NET_PID $IOWAIT_PID $MEM_PID"
}

# Function to stop all monitors
stop_monitors() {
  kill $1 $2 $3 $4 $5 2>/dev/null
  wait $1 $2 $3 $4 $5 2>/dev/null
}

# Record start time
START_TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
echo "Test started at: $START_TIMESTAMP"
echo ""

# Get source data size
echo "Calculating source data size..."
SOURCE_SIZE=$(du -sb /var/lib/phosphor-data-sync/bmc_data_bkp/perf_test/ 2>/dev/null | awk '{print $1}')
if [ -z "$SOURCE_SIZE" ] || [ "$SOURCE_SIZE" -eq 0 ]; then
  echo "WARNING: Source directory is empty or not found!"
  echo "Expected path: /var/lib/phosphor-data-sync/bmc_data_bkp/perf_test/"
  SOURCE_SIZE=0
fi
SOURCE_MB=$(awk -v size="$SOURCE_SIZE" 'BEGIN {printf "%.2f", size/1024/1024}')
echo "Source data size: $SOURCE_MB MB ($SOURCE_SIZE bytes)"
echo ""

# Start monitors
echo "Starting performance monitors..."
MONITOR_PIDS=$(start_monitors)
sleep 2
echo "Monitors started (PIDs: $MONITOR_PIDS)"
echo ""

# Trigger full sync by restarting service
echo "Triggering full sync (restarting service)..."
systemctl restart $SERVICE
if [ $? -ne 0 ]; then
  echo "ERROR: Failed to restart service!"
  stop_monitors $MONITOR_PIDS
  exit 1
fi
echo "Service restarted successfully"
echo ""

# Wait for sync to complete
echo "Waiting for sync to complete..."
echo "(Monitoring journal for completion message)"
echo ""

TIMEOUT=600  # 10 minutes timeout
ELAPSED=0
SYNC_COMPLETE=0

while [ $ELAPSED -lt $TIMEOUT ]; do
  # Check journal for completion message
  if journalctl -u $SERVICE --since "$START_TIMESTAMP" 2>/dev/null | \
     grep -q "Full Sync completed successfully"; then
    SYNC_COMPLETE=1
    break
  fi
  sleep 2
  ELAPSED=$((ELAPSED + 2))
  # Show progress every 10 seconds
  if [ $((ELAPSED % 10)) -eq 0 ]; then
    echo "  ... waiting ($ELAPSED seconds elapsed)"
  fi
done
echo ""

if [ $SYNC_COMPLETE -eq 0 ]; then
  echo "ERROR: Sync did not complete within $TIMEOUT seconds timeout!"
  echo "Check service status: systemctl status $SERVICE"
  stop_monitors $MONITOR_PIDS
  exit 1
fi

echo "Sync completed!"
echo ""

# Stop monitors
echo "Stopping monitors..."
stop_monitors $MONITOR_PIDS
sleep 1
echo "Monitors stopped"
echo ""

# Extract sync time from journal
echo "Extracting metrics from logs..."
SYNC_TIME=$(journalctl -u $SERVICE --since "$START_TIMESTAMP" 2>/dev/null | \
  grep "Full Sync completed successfully" | \
  tail -1 | \
  sed -n 's/.*Elapsed time : \[\([0-9]*\)\].*/\1/p')

if [ -z "$SYNC_TIME" ] || [ "$SYNC_TIME" -eq 0 ]; then
  echo "WARNING: Could not extract sync time from journal"
  echo "Using fallback calculation..."
  END_TIMESTAMP=$(date +%s)
  START_EPOCH=$(date -d "$START_TIMESTAMP" +%s 2>/dev/null || echo 0)
  SYNC_TIME=$((END_TIMESTAMP - START_EPOCH))
  if [ "$SYNC_TIME" -le 0 ]; then
    SYNC_TIME=1
  fi
fi

# Calculate throughput
if [ "$SOURCE_SIZE" -gt 0 ] && [ "$SYNC_TIME" -gt 0 ]; then
  THROUGHPUT=$(awk -v size="$SOURCE_SIZE" -v time="$SYNC_TIME" \
    'BEGIN {printf "%.2f", (size/1024/1024)/time}')
else
  THROUGHPUT="N/A"
fi

# Calculate CPU statistics (values are cumulative jiffies, need delta)
if [ -f "$RESULTS_DIR/cpu.log" ] && [ -s "$RESULTS_DIR/cpu.log" ]; then
  # Get system clock ticks per second (usually 100)
  CLK_TCK=$(getconf CLK_TCK 2>/dev/null || echo 100)

  CPU_STATS=$(awk -F',' -v clk_tck="$CLK_TCK" 'NR>1 {
    # Calculate delta from previous sample (in jiffies)
    delta_rsync = $2 - prev_rsync
    delta_stunnel = $3 - prev_stunnel
    delta_datasync = $4 - prev_datasync

    # Convert to CPU percentage (delta_jiffies / clk_tck = seconds of CPU time)
    # Since we sample every 1 second, CPU% = (delta_jiffies / clk_tck) * 100
    rsync_pct = (delta_rsync / clk_tck) * 100
    stunnel_pct = (delta_stunnel / clk_tck) * 100
    datasync_pct = (delta_datasync / clk_tck) * 100

    rsync_sum += rsync_pct
    stunnel_sum += stunnel_pct
    datasync_sum += datasync_pct
    count++

    if (rsync_pct > rsync_max) rsync_max = rsync_pct
    if (stunnel_pct > stunnel_max) stunnel_max = stunnel_pct
    if (datasync_pct > datasync_max) datasync_max = datasync_pct

    prev_rsync = $2
    prev_stunnel = $3
    prev_datasync = $4
  }
  NR==1 {
    prev_rsync = $2
    prev_stunnel = $3
    prev_datasync = $4
  }
  END {
    if (count > 0)
      printf "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
        rsync_sum/count, rsync_max, stunnel_sum/count, stunnel_max, datasync_sum/count, datasync_max
    else
      printf "0.00,0.00,0.00,0.00,0.00,0.00"
  }' "$RESULTS_DIR/cpu.log")

  RSYNC_AVG=$(echo $CPU_STATS | cut -d',' -f1)
  RSYNC_PEAK=$(echo $CPU_STATS | cut -d',' -f2)
  STUNNEL_AVG=$(echo $CPU_STATS | cut -d',' -f3)
  STUNNEL_PEAK=$(echo $CPU_STATS | cut -d',' -f4)
  DATASYNC_AVG=$(echo $CPU_STATS | cut -d',' -f5)
  DATASYNC_PEAK=$(echo $CPU_STATS | cut -d',' -f6)
else
  RSYNC_AVG="N/A"
  RSYNC_PEAK="N/A"
  STUNNEL_AVG="N/A"
  STUNNEL_PEAK="N/A"
  DATASYNC_AVG="N/A"
  DATASYNC_PEAK="N/A"
fi

# Calculate disk utilization
if [ -f "$RESULTS_DIR/disk.log" ] && [ -s "$RESULTS_DIR/disk.log" ]; then
  DISK_UTIL=$(awk -F',' 'NR>1 {
    delta = $2 - prev_ticks
    util_pct = (delta / 10)
    sum += util_pct
    if(util_pct > max) max = util_pct
    count++
    prev_ticks = $2
  }
  NR==1 {prev_ticks=$2}
  END {printf "%.2f,%.2f", sum/count, max}' "$RESULTS_DIR/disk.log")

  DISK_AVG=$(echo $DISK_UTIL | cut -d',' -f1)
  DISK_PEAK=$(echo $DISK_UTIL | cut -d',' -f2)
else
  DISK_AVG="N/A"
  DISK_PEAK="N/A"
fi

# Calculate network bandwidth and throughput
if [ -f "$RESULTS_DIR/network.log" ] && [ -s "$RESULTS_DIR/network.log" ]; then
  NET_BW=$(awk -F',' 'NR>1 {
    # Calculate bandwidth for this interval (bytes/sec converted to MB/s)
    rx_bw = ($2 - prev_rx) / (1024 * 1024)
    tx_bw = ($3 - prev_tx) / (1024 * 1024)
    total_bw = rx_bw + tx_bw

    rx_sum += rx_bw
    tx_sum += tx_bw
    total_sum += total_bw

    if(rx_bw > rx_max) rx_max = rx_bw
    if(tx_bw > tx_max) tx_max = tx_bw
    if(total_bw > total_max) total_max = total_bw

    count++
    prev_rx = $2
    prev_tx = $3
  }
  NR==1 {prev_rx=$2; prev_tx=$3}
  END {printf "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", rx_sum/count, rx_max, tx_sum/count, tx_max, total_sum/count, total_max}' \
    "$RESULTS_DIR/network.log")

  RX_AVG=$(echo $NET_BW | cut -d',' -f1)
  RX_PEAK=$(echo $NET_BW | cut -d',' -f2)
  TX_AVG=$(echo $NET_BW | cut -d',' -f3)
  TX_PEAK=$(echo $NET_BW | cut -d',' -f4)
  THROUGHPUT_AVG=$(echo $NET_BW | cut -d',' -f5)
  THROUGHPUT_PEAK=$(echo $NET_BW | cut -d',' -f6)
else
  RX_AVG="N/A"
  RX_PEAK="N/A"
  TX_AVG="N/A"
  TX_PEAK="N/A"
  THROUGHPUT_AVG="N/A"
  THROUGHPUT_PEAK="N/A"
fi

# Calculate I/O wait (using delta between samples)
if [ -f "$RESULTS_DIR/iowait.log" ] && [ -s "$RESULTS_DIR/iowait.log" ]; then
  IOWAIT_STATS=$(awk -F',' 'NR>1 {
    # Calculate delta from previous sample
    delta_total = $2 - prev_total
    delta_iowait = $3 - prev_iowait

    # Calculate I/O wait percentage for this interval
    if (delta_total > 0) {
      iowait_pct = (delta_iowait / delta_total) * 100
      sum += iowait_pct
      count++
      if (iowait_pct > max) max = iowait_pct
    }

    prev_total = $2
    prev_iowait = $3
  }
  NR==1 {
    prev_total = $2
    prev_iowait = $3
  }
  END {
    if (count > 0)
      printf "%.2f,%.2f", sum/count, max
    else
      printf "0.00,0.00"
  }' "$RESULTS_DIR/iowait.log")

  IOWAIT_AVG=$(echo $IOWAIT_STATS | cut -d',' -f1)
  IOWAIT_PEAK=$(echo $IOWAIT_STATS | cut -d',' -f2)
else
  IOWAIT_AVG="N/A"
  IOWAIT_PEAK="N/A"
fi

# Calculate memory statistics
if [ -f "$RESULTS_DIR/memory.log" ] && [ -s "$RESULTS_DIR/memory.log" ]; then
  MEM_STATS=$(awk -F',' '
  BEGIN {
    cmd = "awk \"/MemTotal/ {print \\$2}\" /proc/meminfo"
    cmd | getline total_mem
    close(cmd)
  }
  {
    avail = $2
    sum += avail
    count++

    if (NR==1) {
      min = avail
      max = avail
      first = avail
    }
    if (avail < min) min = avail
    if (avail > max) max = avail
    last = avail
  }
  END {
    if (count > 0) {
      avg = sum / count
      min_pct = (min / total_mem) * 100
      decline = first - last
      decline_pct = (decline / first) * 100

      # Assess OOM risk
      if (min_pct < 5)
        risk = "CRITICAL"
      else if (min_pct < 10)
        risk = "HIGH"
      else if (min_pct < 20)
        risk = "MODERATE"
      else
        risk = "LOW"

      # Assess cache pressure
      if (decline_pct > 30)
        pressure = "HIGH"
      else if (decline_pct > 10)
        pressure = "MODERATE"
      else
        pressure = "LOW"

      printf "%d,%d,%d,%.2f,%s,%s", avg, min, max, decline_pct, risk, pressure
    }
  }' "$RESULTS_DIR/memory.log")

  MEM_AVG=$(echo $MEM_STATS | cut -d',' -f1)
  MEM_MIN=$(echo $MEM_STATS | cut -d',' -f2)
  MEM_MAX=$(echo $MEM_STATS | cut -d',' -f3)
  MEM_DECLINE=$(echo $MEM_STATS | cut -d',' -f4)
  OOM_RISK=$(echo $MEM_STATS | cut -d',' -f5)
  CACHE_PRESSURE=$(echo $MEM_STATS | cut -d',' -f6)

  # Convert KB to MB
  MEM_AVG_MB=$(awk -v mem="$MEM_AVG" 'BEGIN {printf "%.2f", mem/1024}')
  MEM_MIN_MB=$(awk -v mem="$MEM_MIN" 'BEGIN {printf "%.2f", mem/1024}')
  MEM_MAX_MB=$(awk -v mem="$MEM_MAX" 'BEGIN {printf "%.2f", mem/1024}')
else
  MEM_AVG_MB="N/A"
  MEM_MIN_MB="N/A"
  MEM_MAX_MB="N/A"
  MEM_DECLINE="N/A"
  OOM_RISK="N/A"
  CACHE_PRESSURE="N/A"
fi

# Save journal logs
journalctl -u $SERVICE --since "$START_TIMESTAMP" > "$RESULTS_DIR/journal.log" 2>/dev/null

# Generate summary report
cat > "$RESULTS_DIR/summary.txt" << EOF
========================================
  Data Sync Performance Test Results
========================================

Test Time: $START_TIMESTAMP
Results Directory: $RESULTS_DIR
Device: $DEVICE
Interface: $INTERFACE

DATA TRANSFER:
  Source Size:      $SOURCE_MB MB
  Sync Time:        $SYNC_TIME seconds
  Throughput:       $THROUGHPUT MB/s

CPU USAGE:
  rsync (SyncBMCData_rsync.service):
    Average:  $RSYNC_AVG%
    Peak:     $RSYNC_PEAK%
  stunnel (SyncBMCData_stunnel.service):
    Average:  $STUNNEL_AVG%
    Peak:     $STUNNEL_PEAK%
  data-sync (xyz.openbmc_project.Control.SyncBMCData.service):
    Average:  $DATASYNC_AVG%
    Peak:     $DATASYNC_PEAK%

DISK I/O:
  Utilization:
    Average:  $DISK_AVG%
    Peak:     $DISK_PEAK%

NETWORK BANDWIDTH:
  RX (Receive):
    Average:  $RX_AVG MB/s
    Peak:     $RX_PEAK MB/s
  TX (Transmit):
    Average:  $TX_AVG MB/s
    Peak:     $TX_PEAK MB/s
  Total Throughput (RX + TX):
    Average:  $THROUGHPUT_AVG MB/s
    Peak:     $THROUGHPUT_PEAK MB/s

I/O WAIT:
  Average:  $IOWAIT_AVG%
  Peak:     $IOWAIT_PEAK%

MEMORY:
  Available Memory:
    Average:  $MEM_AVG_MB MB
    Minimum:  $MEM_MIN_MB MB
    Maximum:  $MEM_MAX_MB MB
  Memory Decline:  $MEM_DECLINE%
  Cache Pressure:  $CACHE_PRESSURE
  OOM Risk:        $OOM_RISK

RAW DATA FILES:
  CPU:      $RESULTS_DIR/cpu.log
  Disk:     $RESULTS_DIR/disk.log
  Network:  $RESULTS_DIR/network.log
  I/O Wait: $RESULTS_DIR/iowait.log
  Memory:   $RESULTS_DIR/memory.log
  Journal:  $RESULTS_DIR/journal.log

========================================
EOF

# Display summary
echo ""
echo "========================================="
echo "  PERFORMANCE TEST RESULTS"
echo "========================================="
echo ""
cat "$RESULTS_DIR/summary.txt"
echo ""
echo "Full report saved to: $RESULTS_DIR/summary.txt"
echo ""
echo "Performance test completed successfully!"

# Made with Bob




