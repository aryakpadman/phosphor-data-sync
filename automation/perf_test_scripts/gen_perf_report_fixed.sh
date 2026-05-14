#!/bin/bash

BASE_DIR="/tmp/perf-test"

# Check if folder name is provided as argument
if [ -n "$1" ]; then
    # If argument is a full path, use it directly
    if [[ "$1" = /* ]]; then
        DIR="$1"
    else
        # Otherwise, treat it as a subdirectory under BASE_DIR
        DIR="$BASE_DIR/$1"
    fi
else
    # If no argument, try to use the latest run
    if [ -f "$BASE_DIR/latest_run" ]; then
        DIR=$(cat "$BASE_DIR/latest_run")
        echo "[INFO] Using latest run directory: $DIR"
    else
        echo "[ERROR] No folder specified and no latest_run file found."
        echo "Usage: $0 [folder_name|full_path]"
        echo "  folder_name: Name of subdirectory under $BASE_DIR (e.g., 20260505_123456)"
        echo "  full_path: Full path to log directory (e.g., /tmp/perf-test/20260505_123456)"
        echo ""
        echo "Available directories:"
        ls -1dt "$BASE_DIR"/*/ 2>/dev/null | head -5 || echo "  No directories found"
        exit 1
    fi
fi

# Verify directory exists
if [ ! -d "$DIR" ]; then
    echo "[ERROR] Directory does not exist: $DIR"
    exit 1
fi

# Verify required log files exist
REQUIRED_FILES=("pidstat_cpu.log" "pidstat_mem.log" "iostat.log" "mpstat.log" "network.log")
MISSING_FILES=()

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$DIR/$file" ]; then
        MISSING_FILES+=("$file")
    fi
done

if [ ${#MISSING_FILES[@]} -gt 0 ]; then
    echo "[WARNING] Some log files are missing in $DIR:"
    printf '  - %s\n' "${MISSING_FILES[@]}"
    echo ""
fi

echo "========================================="
echo "   Data Sync Performance Report"
echo "========================================="
echo "Analyzing logs from: $DIR"
echo "========================================="

# ----------------------------
# CPU usage (process-level)
# ----------------------------
echo ""
echo "[CPU Usage per Process]"

SERVICES=("rsync" "stunnel" "phosphor-rbmc-d" "phosphor-log-m")

for svc in "${SERVICES[@]}"; do
    echo ""
    echo "---- $svc ----"

    awk -v svc="$svc" '
    BEGIN { cpu_col=0; found_header=0 }

    # detect header line dynamically
    $0 ~ "%CPU" {
        for (i=1;i<=NF;i++) {
            if ($i == "%CPU") cpu_col=i
        }
        found_header=1
        next
    }

    # skip until header is found
    found_header==0 { next }

    {
        # pidstat: last column is COMMAND
        cmd = $NF

        if (cmd ~ svc) {
            cpu += $(cpu_col)
            count++
            if ($(cpu_col) > peak) peak = $(cpu_col)
        }
    }

    END {
        if (count > 0) {
            printf "Avg CPU: %.2f%%  Peak CPU: %.2f%%\n", cpu/count, peak
        } else {
            print "No data"
        }
    }' $DIR/pidstat_cpu.log

done


# ----------------------------
# Memory usage (process-level)
# ----------------------------
echo ""
echo "[Memory Usage per Process]"

for svc in "${SERVICES[@]}"; do
    echo ""
    echo "---- $svc ----"

    awk -v svc="$svc" '
    BEGIN { mem_col=0; vsz_col=0; rss_col=0; found_header=0 }

    # detect header line dynamically
    $0 ~ "%MEM" {
        for (i=1;i<=NF;i++) {
            if ($i == "%MEM") mem_col=i
            if ($i == "VSZ") vsz_col=i
            if ($i == "RSS") rss_col=i
        }
        found_header=1
        next
    }

    # skip until header is found
    found_header==0 { next }

    {
        # pidstat: last column is COMMAND
        cmd = $NF

        if (cmd ~ svc) {
            mem += $(mem_col)
            if (vsz_col > 0) vsz += $(vsz_col)
            if (rss_col > 0) rss += $(rss_col)
            count++
            if ($(mem_col) > peak_mem) peak_mem = $(mem_col)
            if (rss_col > 0 && $(rss_col) > peak_rss) peak_rss = $(rss_col)
        }
    }

    END {
        if (count > 0) {
            printf "Avg MEM: %.2f%%  Peak MEM: %.2f%%\n", mem/count, peak_mem
            if (rss_col > 0) {
                printf "Avg RSS: %.2f kB  Peak RSS: %.2f kB\n", rss/count, peak_rss
            }
        } else {
            print "No data"
        }
    }' $DIR/pidstat_mem.log

done


# ----------------------------
# Disk stats (sda)
# ----------------------------
echo ""
echo "[Disk Stats - sda]"

grep sda $DIR/iostat.log > $DIR/iostat_filtered.log

awk '
{
    util += $NF
    await += $(NF-1)
    count++
    if ($NF > peak_util) peak_util=$NF
}
END {
    if (count > 0) {
        printf "Avg util: %.2f%%\n", util/count
        printf "Peak util: %.2f%%\n", peak_util
        printf "Avg await: %.2f ms\n", await/count
    } else {
        print "No disk data found"
    }
}' $DIR/iostat_filtered.log


# ----------------------------
# CPU IO wait
# ----------------------------
echo ""
echo "[CPU IO Wait]"

grep "all" $DIR/mpstat.log | awk '
NR==1 {
    for (i=1;i<=NF;i++) {
        if ($i == "%iowait") col=i
    }
    next
}
{
    iowait += $(col)
    count++
}
END {
    if (count > 0)
        printf "Avg iowait: %.2f%%\n", iowait/count
    else
        print "No mpstat data"
}'


# ----------------------------
# Network stats
# ----------------------------
echo ""
echo "[Network Throughput]"

grep -E "eth2" $DIR/network.log > $DIR/net_filtered.log

awk '
{
    rx += $5
    tx += $6
    count++
}
END {
    if (count > 0) {
        printf "Avg RX: %.2f kB/s\n", rx/count
        printf "Avg TX: %.2f kB/s\n", tx/count
    } else {
        print "No network data"
    }
}' $DIR/net_filtered.log


echo ""
echo "========================================="
echo " Logs analyzed from: $DIR"
echo "========================================="
