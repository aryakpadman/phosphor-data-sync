#!/bin/bash

BASE_OUTPUT_DIR="/tmp/perf-test"
INTERVAL=1

PIDSTAT_CPU=""
PIDSTAT_MEM=""
IOSTAT_PID=""
MPSTAT_PID=""
SAR_PID=""

start_monitoring() {
    echo "[INFO] Starting performance monitoring..."

    # Create timestamped directory
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTPUT_DIR="$BASE_OUTPUT_DIR/$TIMESTAMP"

    mkdir -p "$OUTPUT_DIR"

    LOG_FILE="$OUTPUT_DIR/monitor.log"
    echo "=== Monitoring Started at $(date) ===" >> "$LOG_FILE"
    echo "[INFO] Logs will be saved to: $OUTPUT_DIR" | tee -a "$LOG_FILE"

    # ----------------------------
    # CPU monitoring (FIXED)
    # ----------------------------
    # Removed -h flag to capture all processes including idle ones
    pidstat -u $INTERVAL > "$OUTPUT_DIR/pidstat_cpu.log" &
    PIDSTAT_CPU=$!
    sleep 1
    kill -0 $PIDSTAT_CPU 2>/dev/null && \
        echo "[OK] pidstat CPU started (PID=$PIDSTAT_CPU)" | tee -a "$LOG_FILE" || \
        echo "[ERROR] pidstat CPU failed" | tee -a "$LOG_FILE"

    # ----------------------------
    # Memory monitoring
    # ----------------------------
    # Removed -h flag to capture all processes including idle ones
    pidstat -r $INTERVAL > "$OUTPUT_DIR/pidstat_mem.log" &
    PIDSTAT_MEM=$!
    sleep 1
    kill -0 $PIDSTAT_MEM 2>/dev/null && \
        echo "[OK] pidstat MEM started (PID=$PIDSTAT_MEM)" | tee -a "$LOG_FILE" || \
        echo "[ERROR] pidstat MEM failed" | tee -a "$LOG_FILE"

    # ----------------------------
    # Disk monitoring
    # ----------------------------
    iostat -dx $INTERVAL > "$OUTPUT_DIR/iostat.log" &
    IOSTAT_PID=$!
    sleep 1
    kill -0 $IOSTAT_PID 2>/dev/null && \
        echo "[OK] iostat started (PID=$IOSTAT_PID)" | tee -a "$LOG_FILE" || \
        echo "[ERROR] iostat failed" | tee -a "$LOG_FILE"

    # ----------------------------
    # CPU system-wide
    # ----------------------------
    mpstat $INTERVAL > "$OUTPUT_DIR/mpstat.log" &
    MPSTAT_PID=$!
    sleep 1
    kill -0 $MPSTAT_PID 2>/dev/null && \
        echo "[OK] mpstat started (PID=$MPSTAT_PID)" | tee -a "$LOG_FILE" || \
        echo "[ERROR] mpstat failed" | tee -a "$LOG_FILE"

    # ----------------------------
    # Network monitoring
    # ----------------------------
    sar -n DEV $INTERVAL > "$OUTPUT_DIR/network.log" &
    SAR_PID=$!
    sleep 1
    kill -0 $SAR_PID 2>/dev/null && \
        echo "[OK] sar started (PID=$SAR_PID)" | tee -a "$LOG_FILE" || \
        echo "[ERROR] sar failed" | tee -a "$LOG_FILE"

    # Save PIDs and output directory path
    echo "$PIDSTAT_CPU $PIDSTAT_MEM $IOSTAT_PID $MPSTAT_PID $SAR_PID" > "$OUTPUT_DIR/pids"
    echo "$OUTPUT_DIR" > "$BASE_OUTPUT_DIR/latest_run"

    echo "[SUCCESS] Monitoring started. Logs in $OUTPUT_DIR"
}

stop_monitoring() {
    echo "[INFO] Stopping monitoring..."

    # Find the latest run directory
    if [ ! -f "$BASE_OUTPUT_DIR/latest_run" ]; then
        echo "[ERROR] No latest_run file found. Cannot determine which monitoring session to stop."
        exit 1
    fi

    OUTPUT_DIR=$(cat "$BASE_OUTPUT_DIR/latest_run")

    if [ ! -f "$OUTPUT_DIR/pids" ]; then
        echo "[ERROR] No PID file found in $OUTPUT_DIR"
        exit 1
    fi

    LOG_FILE="$OUTPUT_DIR/monitor.log"
    read PIDSTAT_CPU PIDSTAT_MEM IOSTAT_PID MPSTAT_PID SAR_PID < "$OUTPUT_DIR/pids"

    kill $PIDSTAT_CPU $PIDSTAT_MEM $IOSTAT_PID $MPSTAT_PID $SAR_PID 2>/dev/null

    echo "=== Monitoring Stopped at $(date) ===" >> "$LOG_FILE"
    echo "[SUCCESS] Monitoring stopped. Logs saved in: $OUTPUT_DIR"
}

case "$1" in
    start)
        start_monitoring
        ;;
    stop)
        stop_monitoring
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        ;;
esac
