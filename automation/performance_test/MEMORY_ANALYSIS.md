# Memory Analysis: Cache Pressure and OOM Risk

## Understanding Memory Metrics

### What We Track
```bash
awk '/MemAvailable/ {print ts","$2}' /proc/meminfo
```

This logs **MemAvailable** in KB, which represents:
- Memory available for starting new applications
- Includes free memory + reclaimable cache/buffers
- Best indicator of actual usable memory

## Key Memory Metrics from /proc/meminfo

```bash
cat /proc/meminfo
```

Important fields:
```
MemTotal:        2048000 kB   # Total physical RAM
MemFree:          500000 kB   # Completely unused memory
MemAvailable:    1200000 kB   # Available for new apps (includes reclaimable)
Buffers:          100000 kB   # Disk cache buffers
Cached:           600000 kB   # Page cache
```

---

## Calculations

### 1. Memory Usage Percentage
```bash
# Calculate memory usage
awk '/MemTotal/ {total=$2} /MemAvailable/ {avail=$2}
END {
  used = total - avail
  usage_pct = (used / total) * 100
  print "Memory Usage: " usage_pct "%"
}' /proc/meminfo
```

**Interpretation:**
- **0-50%**: Low usage, plenty of memory
- **50-75%**: Moderate usage, normal
- **75-90%**: High usage, watch for pressure
- **90%+**: Critical, OOM risk

### 2. Cache Pressure Detection

**Method 1: Available Memory Trend**
```bash
# Analyze memory.log for declining trend
awk -F',' 'NR==1 {first=$2} END {last=$2;
  decline = first - last
  decline_pct = (decline / first) * 100
  print "Memory decline: " decline_pct "%"
}' memory.log
```

**Interpretation:**
- **Decline < 10%**: No pressure
- **Decline 10-30%**: Moderate pressure
- **Decline > 30%**: High cache pressure

**Method 2: Minimum Available Memory**
```bash
# Find minimum available memory during sync
awk -F',' '{if(NR==1 || $2<min) min=$2}
END {print "Min available: " min " KB"}' memory.log
```

**Thresholds (for typical BMC with 2GB RAM):**
- **> 500 MB**: Safe
- **200-500 MB**: Moderate pressure
- **< 200 MB**: High pressure, OOM risk

### 3. OOM (Out of Memory) Risk Assessment

**Risk Score Calculation:**
```bash
# Calculate OOM risk based on minimum available memory
awk -F',' '
BEGIN {
  # Get total memory (in KB)
  cmd = "awk \"/MemTotal/ {print \\$2}\" /proc/meminfo"
  cmd | getline total_mem
  close(cmd)
}
{
  if(NR==1 || $2<min) min=$2
}
END {
  min_pct = (min / total_mem) * 100

  if (min_pct < 5)
    risk = "CRITICAL - OOM imminent"
  else if (min_pct < 10)
    risk = "HIGH - OOM likely"
  else if (min_pct < 20)
    risk = "MODERATE - Watch closely"
  else
    risk = "LOW - Safe"

  printf "Min available: %.2f%% (%d KB)\n", min_pct, min
  print "OOM Risk: " risk
}' memory.log
```

---

## Complete Memory Analysis Script

Add this to the performance test script:

```bash
# Calculate memory statistics
if [ -f "$RESULTS_DIR/memory.log" ] && [ -s "$RESULTS_DIR/memory.log" ]; then
  MEM_STATS=$(awk -F',' '
  BEGIN {
    # Get total memory
    cmd = "awk \"/MemTotal/ {print \\$2}\" /proc/meminfo"
    cmd | getline total_mem
    close(cmd)
  }
  {
    avail = $2
    sum += avail
    count++

    # Track min and max
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

      # Calculate percentages
      avg_pct = (avg / total_mem) * 100
      min_pct = (min / total_mem) * 100

      # Calculate decline
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

      # Output: avg_kb,min_kb,max_kb,decline_pct,risk,pressure
      printf "%d,%d,%d,%.2f,%s,%s", avg, min, max, decline_pct, risk, pressure
    }
  }' "$RESULTS_DIR/memory.log")

  MEM_AVG=$(echo $MEM_STATS | cut -d',' -f1)
  MEM_MIN=$(echo $MEM_STATS | cut -d',' -f2)
  MEM_MAX=$(echo $MEM_STATS | cut -d',' -f3)
  MEM_DECLINE=$(echo $MEM_STATS | cut -d',' -f4)
  OOM_RISK=$(echo $MEM_STATS | cut -d',' -f5)
  CACHE_PRESSURE=$(echo $MEM_STATS | cut -d',' -f6)

  # Convert KB to MB for display
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
```

---

## Report Format

Add to summary report:

```bash
MEMORY:
  Available Memory:
    Average:  ${MEM_AVG_MB} MB
    Minimum:  ${MEM_MIN_MB} MB
    Maximum:  ${MEM_MAX_MB} MB
  Memory Decline:  ${MEM_DECLINE}%
  Cache Pressure:  ${CACHE_PRESSURE}
  OOM Risk:        ${OOM_RISK}
```

---

## Interpretation Guide

### Cache Pressure Levels

**LOW** (< 10% decline):
- System has plenty of memory
- Cache is stable
- No performance impact

**MODERATE** (10-30% decline):
- System is using more memory
- Some cache eviction occurring
- Monitor but not critical

**HIGH** (> 30% decline):
- Significant memory consumption
- Heavy cache eviction
- May impact performance
- Consider reducing sync load

### OOM Risk Levels

**LOW** (> 20% available):
- Safe operating range
- No OOM risk

**MODERATE** (10-20% available):
- Watch memory usage
- OOM possible under load spikes

**HIGH** (5-10% available):
- OOM likely if memory usage increases
- Reduce concurrent operations

**CRITICAL** (< 5% available):
- OOM imminent
- System may kill processes
- Stop non-essential services

---

## Example Output

```
MEMORY:
  Available Memory:
    Average:  1200.50 MB
    Minimum:  800.25 MB
    Maximum:  1500.75 MB
  Memory Decline:  15.5%
  Cache Pressure:  MODERATE
  OOM Risk:        LOW
```

**Analysis:**
- Started with ~1500 MB available
- Dropped to ~800 MB minimum (still safe)
- 15.5% decline indicates moderate cache pressure
- OOM risk is low (800 MB is > 20% of typical 2GB BMC)

---

## Recommendations

### If Cache Pressure is HIGH:
1. Reduce sync batch size
2. Add delays between operations
3. Clear caches before sync: `sync; echo 3 > /proc/sys/vm/drop_caches`

### If OOM Risk is HIGH/CRITICAL:
1. Stop non-essential services before sync
2. Reduce concurrent rsync processes
3. Consider increasing BMC memory (hardware)
4. Split sync into smaller chunks

---

## Additional Monitoring

For more detailed analysis, also track:

```bash
# Track memory breakdown
awk '/MemTotal/||/MemFree/||/MemAvailable/||/Buffers/||/Cached/ {
  print ts","$1","$2
}' /proc/meminfo >> memory_detail.log
```

This helps identify:
- Is memory used by cache (reclaimable)?
- Is memory used by applications (not reclaimable)?
- Can system free up memory if needed?
