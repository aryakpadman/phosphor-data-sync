# Performance Test for phosphor-data-sync

## Overview
This directory contains a performance testing script for measuring BMC data sync performance metrics.

## Files
- `perf_test.sh` - Main performance test script
- `BUSYBOX_TOOLS_REQUIRED.md` - List of required BusyBox tools

## Quick Start

### 1. Run on Active BMC
```bash
cd automation/performance_test
chmod +x perf_test.sh
./perf_test.sh
```

The script will use default values:
- Device: `sda` (UFS storage)
- Interface: `eth2` (network interface)

### 2. Custom Device/Interface
```bash
./perf_test.sh <device> <interface>

# Examples:
./perf_test.sh sda eth2      # Default for IBM BMC
./perf_test.sh mmcblk0 eth0  # For eMMC-based BMCs
./perf_test.sh nvme0n1 usb0  # For NVMe with USB network
```

## What It Measures

The script collects the following metrics during a full sync:

1. **Total Sync Time** - Extracted from journal logs (seconds)
2. **Throughput** - Data transfer rate (MB/s)
3. **CPU Usage** - Average and peak for rsync and stunnel (%)
4. **Disk Utilization** - Average and peak disk I/O (%)
5. **Network Bandwidth** - Average and peak RX/TX (MB/s)
6. **I/O Wait** - Average and peak CPU I/O wait (%)

## Output

Results are saved to `/tmp/perf_results_YYYYMMDD_HHMMSS/` containing:
- `summary.txt` - Performance metrics summary
- `cpu.log` - Raw CPU usage samples
- `disk.log` - Raw disk I/O samples
- `network.log` - Raw network bandwidth samples
- `iowait.log` - Raw I/O wait samples
- `journal.log` - Service journal logs

## Example Output

```
=========================================
  Data Sync Performance Test Results
=========================================

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

## Prerequisites

### Required Tools (usually pre-installed on OpenBMC):
- `date`, `ps`, `grep`, `awk`, `cat`, `du`
- `journalctl`, `systemctl`
- Access to `/proc/stat`, `/proc/net/dev`, `/sys/block/*/stat`

### Verify Prerequisites:
```bash
# Check if all required tools are available
for tool in date ps grep awk cat du journalctl systemctl; do
  which $tool && echo "✓ $tool" || echo "✗ $tool MISSING"
done
```

## How It Works

1. **Start Monitoring** - Launches 4 background processes that sample metrics every second
2. **Trigger Sync** - Restarts the data-sync service to initiate full sync
3. **Wait for Completion** - Monitors journal for "Full Sync completed successfully"
4. **Stop Monitoring** - Terminates the monitoring processes
5. **Calculate Metrics** - Processes raw logs to compute averages and peaks
6. **Generate Report** - Creates summary and saves all data

## Important Notes

### Before Running:
1. **Clear destination on passive BMC** for accurate full sync test:
   ```bash
   # On PASSIVE BMC
   rm -rf /var/lib/phosphor-data-sync/bmc_data_bkp/perf_test/*
   ```

2. **Run on ACTIVE BMC** - The script must run on the active BMC

3. **Ensure sufficient space** - `/tmp` needs ~1-2 MB for logs

### Device Configuration:
- **IBM BMC (UFS)**: Use `sda` (default)
- **eMMC BMC**: Use `mmcblk0`
- **NVMe BMC**: Use `nvme0n1`

To find your device:
```bash
ls /sys/block/ | grep -E 'sda|mmcblk|nvme'
```

### Network Interface:
- **IBM BMC**: Usually `eth2` (default)
- **Other BMCs**: May use `eth0`, `usb0`, etc.

To find your interface:
```bash
cat /proc/net/dev | grep ':' | awk -F: '{print $1}' | grep -v lo
```

## Troubleshooting

### Script fails to find device
```bash
# List available devices
ls -la /sys/block/

# Update script or pass correct device
./perf_test.sh <correct_device> eth2
```

### Script fails to find network interface
```bash
# List available interfaces
cat /proc/net/dev

# Update script or pass correct interface
./perf_test.sh sda <correct_interface>
```

### Sync doesn't complete
```bash
# Check service status
systemctl status xyz.openbmc_project.Control.SyncBMCData.service

# Check for errors
journalctl -u xyz.openbmc_project.Control.SyncBMCData.service -f
```

### No data in logs
- Verify device name is correct: `cat /sys/block/sda/stat`
- Verify interface name is correct: `cat /proc/net/dev | grep eth2`
- Check if rsync/stunnel are running: `ps aux | grep -E 'rsync|stunnel'`

## Advanced Usage

### Run multiple tests
```bash
for i in 1 2 3; do
  echo "Running test $i..."
  ./perf_test.sh
  sleep 60  # Wait between tests
done
```

### Compare results
```bash
# View all test results
ls -la /tmp/perf_results_*/summary.txt

# Compare sync times
grep "Sync Time:" /tmp/perf_results_*/summary.txt
```

## BusyBox Compatibility

This script is designed for BusyBox environments and uses only:
- Basic shell commands (no bash-specific features)
- `/proc` and `/sys` filesystems (no iostat, sar, etc.)
- Simple awk for calculations (no bc required)

See `BUSYBOX_TOOLS_REQUIRED.md` for detailed tool requirements.

## License

SPDX-License-Identifier: Apache-2.0