# BMC Data Sync Performance Testing Guide

## Overview
This guide explains how to perform performance testing on the phosphor-data-sync application running on a BusyBox-based BMC.

---

## Metrics Collection Strategy

### 1. Total Sync Time ✓ (From Journal)
**Source:** Application journal log

The application already logs the elapsed time:
```
Apr 27 10:17:10 rbmc-prototype phosphor-rbmc-data-sync-mgr[815]: Full Sync completed successfully. Elapsed time : [45] seconds
```

**Extraction:**
```bash
# Get elapsed time from journal
SYNC_TIME=$(journalctl -u xyz.openbmc_project.Control.SyncBMCData.service \
  --since "1 minute ago" | \
  grep "Full Sync completed successfully" | \
  tail -1 | \
  sed -n 's/.*Elapsed time : \[\([0-9]*\)\].*/\1/p')

echo "Total Sync Time: $SYNC_TIME seconds"
```

---

### 2. Throughput (MB/s)
**Calculation:** Data size / Sync time

```bash
# Get source data size
SOURCE_SIZE=$(du -sb /var/lib/phosphor-data-sync/bmc_data_bkp/perf_test/ 2>/dev/null | awk '{print $1}')

# Convert to MB and calculate throughput
THROUGHPUT=$(awk -v size="$SOURCE_SIZE" -v time="$SYNC_TIME" \
  'BEGIN {printf "%.2f", (size/1024/1024)/time}')

echo "Throughput: $THROUGHPUT MB/s"
```

---

### 3. CPU Usage (rsync & stunnel)
**Method:** Sample every 1 second during sync, calculate average

**Data Collection:**
```bash
# Monitor CPU usage
while true; do
  TIMESTAMP=$(date +%s)
  RSYNC_CPU=$(ps aux | grep '[r]sync' | awk '{sum+=$3} END {print sum+0}')
  STUNNEL_CPU=$(ps aux | grep '[s]tunnel' | awk '{sum+=$3} END {print sum+0}')
  echo "$TIMESTAMP,$RSYNC_CPU,$STUNNEL_CPU" >> /tmp/perf/cpu.log
  sleep 1
done &
CPU_MONITOR_PID=$!
```

**Calculation:**
```bash
# Average CPU usage
awk -F',' '{rsync_sum+=$2; stunnel_sum+=$3; count++}
END {
  printf "rsync avg: %.2f%%, stunnel avg: %.2f%%\n",
  rsync_sum/count, stunnel_sum/count
}' /tmp/perf/cpu.log

# Peak CPU usage
awk -F',' '{
  if($2>rsync_max) rsync_max=$2
  if($3>stunnel_max) stunnel_max=$3
}
END {
  printf "rsync peak: %.2f%%, stunnel peak: %.2f%%\n",
  rsync_max, stunnel_max
}' /tmp/perf/cpu.log
```

---

### 4. Disk Utilization (%util)
**Method:** Monitor I/O ticks from `/sys/block/[device]/stat`

**Data Collection:**
```bash
DEVICE="mmcblk0"  # Adjust for your system

while true; do
  TIMESTAMP=$(date +%s)
  # Field 13 is io_ticks (time spent doing I/Os in ms)
  IO_TICKS=$(awk '{print $13}' /sys/block/$DEVICE/stat)
  echo "$TIMESTAMP,$IO_TICKS" >> /tmp/perf/disk.log
  sleep 1
done &
DISK_MONITOR_PID=$!
```

**Calculation:**
```bash
# Calculate average disk utilization
awk -F',' 'NR>1 {
  delta = $2 - prev_ticks
  util_pct = (delta / 10)  # Convert ms to percentage
  sum += util_pct
  count++
  prev_ticks = $2
}
NR==1 {prev_ticks=$2}
END {printf "Avg disk util: %.2f%%\n", sum/count}' /tmp/perf/disk.log
```

---

### 5. Network Bandwidth Usage
**Method:** Monitor `/proc/net/dev` for RX/TX bytes

**Data Collection:**
```bash
INTERFACE="eth0"  # Adjust for your system

while true; do
  TIMESTAMP=$(date +%s)
  # Extract RX and TX bytes
  cat /proc/net/dev | grep "$INTERFACE:" | \
    awk -v ts="$TIMESTAMP" '{print ts","$2","$10}' >> /tmp/perf/network.log
  sleep 1
done &
NET_MONITOR_PID=$!
```

**Calculation:**
```bash
# Calculate average bandwidth
awk -F',' 'NR>1 {
  rx_bw = ($2 - prev_rx) / (1024 * 1024)  # MB/s
  tx_bw = ($3 - prev_tx) / (1024 * 1024)  # MB/s
  rx_sum += rx_bw
  tx_sum += tx_bw
  count++
  prev_rx = $2
  prev_tx = $3
}
NR==1 {prev_rx=$2; prev_tx=$3}
END {
  printf "Avg RX: %.2f MB/s, Avg TX: %.2f MB/s\n",
  rx_sum/count, tx_sum/count
}' /tmp/perf/network.log
```

---

### 6. I/O Wait %
**Method:** Monitor CPU I/O wait from `/proc/stat`

**Data Collection:**
```bash
while true; do
  TIMESTAMP=$(date +%s)
  # Calculate I/O wait percentage
  awk -v ts="$TIMESTAMP" '/^cpu / {
    total = $2+$3+$4+$5+$6+$7+$8
    iowait_pct = ($5/total)*100
    print ts","iowait_pct
  }' /proc/stat >> /tmp/perf/iowait.log
  sleep 1
done &
IOWAIT_MONITOR_PID=$!
```

**Calculation:**
```bash
# Average I/O wait
awk -F',' '{sum+=$2; count++}
END {printf "Avg I/O wait: %.2f%%\n", sum/count}' /tmp/perf/iowait.log
```

---

## Complete Performance Test Script

**Save this as `/tmp/perf_test.sh` on your BMC:**

```bash
#!/bin/sh
# Performance test script for BMC data sync
# Run this on the ACTIVE BMC before triggering full sync

RESULTS_DIR="/tmp/perf_results_$(date +%Y%m%d_%H%M%S)"
DEVICE="mmcblk0"      # Adjust for your system
INTERFACE="eth0"      # Adjust for your system
SERVICE="xyz.openbmc_project.Control.SyncBMCData.service"

echo "========================================="
echo "  Data Sync Performance Test"
echo "========================================="
echo ""
echo "Results directory: $RESULTS_DIR"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to start monitors
start_monitors() {
  # CPU monitor
  while true; do
    TIMESTAMP=$(date +%s)
    RSYNC_CPU=$(ps aux | grep '[r]sync' | awk '{sum+=$3} END {print sum+0}')
    STUNNEL_CPU=$(ps aux | grep '[s]tunnel' | awk '{sum+=$3} END {print sum+0}')
    echo "$TIMESTAMP,$RSYNC_CPU,$STUNNEL_CPU" >> "$RESULTS_DIR/cpu.log"
    sleep 1
  done &
  CPU_PID=$!

  # Disk I/O monitor
  while true; do
    TIMESTAMP=$(date +%s)
    IO_TICKS=$(awk '{print $13}' /sys/block/$DEVICE/stat 2>/dev/null || echo 0)
    echo "$TIMESTAMP,$IO_TICKS" >> "$RESULTS_DIR/disk.log"
    sleep 1
  done &
  DISK_PID=$!

  # Network monitor
  while true; do
    TIMESTAMP=$(date +%s)
    cat /proc/net/dev | grep "$INTERFACE:" | \
      awk -v ts="$TIMESTAMP" '{print ts","$2","$10}' >> "$RESULTS_DIR/network.log"
    sleep 1
  done &
  NET_PID=$!

  # I/O wait monitor
  while true; do
    TIMESTAMP=$(date +%s)
    awk -v ts="$TIMESTAMP" '/^cpu / {
      total = $2+$3+$4+$5+$6+$7+$8
      iowait_pct = ($5/total)*100
      print ts","iowait_pct
    }' /proc/stat >> "$RESULTS_DIR/iowait.log"
    sleep 1
  done &
  IOWAIT_PID=$!

  echo "$CPU_PID $DISK_PID $NET_PID $IOWAIT_PID"
}

# Function to stop monitors
stop_monitors() {
  kill $1 $2 $3 $4 2>/dev/null
  wait $1 $2 $3 $4 2>/dev/null
}

# Record start time
START_TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
echo "Test started at: $START_TIMESTAMP"
echo ""

# Get source data size
echo "Calculating source data size..."
SOURCE_SIZE=$(du -sb /var/lib/phosphor-data-sync/bmc_data_bkp/perf_test/ 2>/dev/null | awk '{print $1}')
SOURCE_MB=$(awk -v size="$SOURCE_SIZE" 'BEGIN {printf "%.2f", size/1024/1024}')
echo "Source data size: $SOURCE_MB MB"
echo ""

# Start monitors
echo "Starting performance monitors..."
MONITOR_PIDS=$(start_monitors)
sleep 2

# Trigger full sync
echo "Triggering full sync (restarting service)..."
systemctl restart $SERVICE

# Wait for sync to complete
echo "Waiting for sync to complete..."
echo "(Monitoring journal for completion message)"

TIMEOUT=600  # 10 minutes
ELAPSED=0
SYNC_COMPLETE=0

while [ $ELAPSED -lt $TIMEOUT ]; do
  # Check journal for completion
  if journalctl -u $SERVICE --since "$START_TIMESTAMP" | \
     grep -q "Full Sync completed successfully"; then
    SYNC_COMPLETE=1
    break
  fi
  sleep 2
  ELAPSED=$((ELAPSED + 2))
  echo -n "."
done
echo ""

if [ $SYNC_COMPLETE -eq 0 ]; then
  echo "ERROR: Sync did not complete within timeout!"
  stop_monitors $MONITOR_PIDS
  exit 1
fi

echo "Sync completed!"
echo ""

# Stop monitors
echo "Stopping monitors..."
stop_monitors $MONITOR_PIDS
sleep 1

# Extract sync time from journal
echo "Extracting metrics..."
SYNC_TIME=$(journalctl -u $SERVICE --since "$START_TIMESTAMP" | \
  grep "Full Sync completed successfully" | \
  tail -1 | \
  sed -n 's/.*Elapsed time : \[\([0-9]*\)\].*/\1/p')

if [ -z "$SYNC_TIME" ] || [ "$SYNC_TIME" -eq 0 ]; then
  echo "WARNING: Could not extract sync time from journal, using 1 second"
  SYNC_TIME=1
fi

# Calculate throughput
THROUGHPUT=$(awk -v size="$SOURCE_SIZE" -v time="$SYNC_TIME" \
  'BEGIN {printf "%.2f", (size/1024/1024)/time}')

# Calculate CPU averages
CPU_STATS=$(awk -F',' '{rsync_sum+=$2; stunnel_sum+=$3; count++
  if($2>rsync_max) rsync_max=$2
  if($3>stunnel_max) stunnel_max=$3
}
END {
  printf "%.2f,%.2f,%.2f,%.2f",
  rsync_sum/count, rsync_max, stunnel_sum/count, stunnel_max
}' "$RESULTS_DIR/cpu.log")

RSYNC_AVG=$(echo $CPU_STATS | cut -d',' -f1)
RSYNC_PEAK=$(echo $CPU_STATS | cut -d',' -f2)
STUNNEL_AVG=$(echo $CPU_STATS | cut -d',' -f3)
STUNNEL_PEAK=$(echo $CPU_STATS | cut -d',' -f4)

# Calculate disk utilization
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

# Calculate network bandwidth
NET_BW=$(awk -F',' 'NR>1 {
  rx_bw = ($2 - prev_rx) / (1024 * 1024)
  tx_bw = ($3 - prev_tx) / (1024 * 1024)
  rx_sum += rx_bw
  tx_sum += tx_bw
  if(rx_bw > rx_max) rx_max = rx_bw
  if(tx_bw > tx_max) tx_max = tx_bw
  count++
  prev_rx = $2
  prev_tx = $3
}
NR==1 {prev_rx=$2; prev_tx=$3}
END {printf "%.2f,%.2f,%.2f,%.2f", rx_sum/count, rx_max, tx_sum/count, tx_max}' \
  "$RESULTS_DIR/network.log")

RX_AVG=$(echo $NET_BW | cut -d',' -f1)
RX_PEAK=$(echo $NET_BW | cut -d',' -f2)
TX_AVG=$(echo $NET_BW | cut -d',' -f3)
TX_PEAK=$(echo $NET_BW | cut -d',' -f4)

# Calculate I/O wait
IOWAIT_STATS=$(awk -F',' '{sum+=$2; count++
  if($2>max) max=$2
}
END {printf "%.2f,%.2f", sum/count, max}' "$RESULTS_DIR/iowait.log")

IOWAIT_AVG=$(echo $IOWAIT_STATS | cut -d',' -f1)
IOWAIT_PEAK=$(echo $IOWAIT_STATS | cut -d',' -f2)

# Save journal logs
journalctl -u $SERVICE --since "$START_TIMESTAMP" > "$RESULTS_DIR/journal.log"

# Generate summary report
cat > "$RESULTS_DIR/summary.txt" << EOF
========================================
  Data Sync Performance Test Results
========================================

Test Time: $START_TIMESTAMP
Results Directory: $RESULTS_DIR

DATA TRANSFER:
  Source Size:      $SOURCE_MB MB
  Sync Time:        $SYNC_TIME seconds
  Throughput:       $THROUGHPUT MB/s

CPU USAGE:
  rsync:
    Average:  $RSYNC_AVG%
    Peak:     $RSYNC_PEAK%
  stunnel:
    Average:  $STUNNEL_AVG%
    Peak:     $STUNNEL_PEAK%

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

I/O WAIT:
  Average:  $IOWAIT_AVG%
  Peak:     $IOWAIT_PEAK%

RAW DATA FILES:
  CPU:      $RESULTS_DIR/cpu.log
  Disk:     $RESULTS_DIR/disk.log
  Network:  $RESULTS_DIR/network.log
  I/O Wait: $RESULTS_DIR/iowait.log
  Journal:  $RESULTS_DIR/journal.log

========================================
EOF

# Display summary
cat "$RESULTS_DIR/summary.txt"

echo ""
echo "Performance test completed!"
echo "Full report saved to: $RESULTS_DIR/summary.txt"
```

---

## Usage Instructions

### 1. Prepare the Script
```bash
# On your BMC, create the script
cat > /tmp/perf_test.sh << 'EOF'
[paste the script above]
EOF

chmod +x /tmp/perf_test.sh
```

### 2. Adjust Configuration
Edit the script to match your system:
```bash
vi /tmp/perf_test.sh

# Change these lines:
DEVICE="mmcblk0"      # Your disk device (sda, nvme0n1, etc.)
INTERFACE="eth0"      # Your network interface (usb0, etc.)
```

### 3. Clear Destination (for fresh full sync)
```bash
# On PASSIVE BMC, clear destination
rm -rf /var/lib/phosphor-data-sync/bmc_data_bkp/perf_test/*
```

### 4. Run the Test
```bash
# On ACTIVE BMC
/tmp/perf_test.sh
```

### 5. View Results
```bash
# Results are in /tmp/perf_results_YYYYMMDD_HHMMSS/
ls -la /tmp/perf_results_*/

# View summary
cat /tmp/perf_results_*/summary.txt
```

---

## Expected Output

```
=========================================
  Data Sync Performance Test Results
=========================================

Test Time: 2026-04-27 10:15:30
Results Directory: /tmp/perf_results_20260427_101530

DATA TRANSFER:
  Source Size:      500.00 MB
  Sync Time:        45 seconds
  Throughput:       11.11 MB/s

CPU USAGE:
  rsync:
    Average:  15.37%
    Peak:     18.50%
  stunnel:
    Average:  8.30%
    Peak:     10.20%

DISK I/O:
  Utilization:
    Average:  45.67%
    Peak:     78.90%

NETWORK BANDWIDTH:
  RX (Receive):
    Average:  10.45 MB/s
    Peak:     15.67 MB/s
  TX (Transmit):
    Average:  0.23 MB/s
    Peak:     0.45 MB/s

I/O WAIT:
  Average:  12.34%
  Peak:     18.90%

=========================================
```

---

## Troubleshooting

### Script fails to find device
```bash
# List available block devices
ls -la /sys/block/

# Update DEVICE variable in script
```

### Script fails to find network interface
```bash
# List available interfaces
cat /proc/net/dev

# Update INTERFACE variable in script
```

### Sync doesn't complete
```bash
# Check service status
systemctl status xyz.openbmc_project.Control.SyncBMCData.service

# Check journal for errors
journalctl -u xyz.openbmc_project.Control.SyncBMCData.service -f
```

---

## Notes

1. **Run on Active BMC** - The script must run on the active BMC
2. **Fresh Sync** - Clear destination on passive BMC for accurate full sync test
3. **Disk Space** - Ensure /tmp has enough space for logs (~1-2 MB)
4. **BusyBox Compatible** - Script uses only BusyBox-compatible commands
5. **No External Tools** - No need for bc, iostat, sar, etc.