# Required Tools for Performance Testing on BusyBox BMC

## Core Requirements Analysis

### 1. MUST HAVE (Critical for basic functionality)

| Tool | Purpose | Check Command | Install if Missing |
|------|---------|---------------|-------------------|
| `date` | Timestamp tracking | `which date` | Built-in to BusyBox |
| `ps` | Process monitoring | `which ps` | Built-in to BusyBox |
| `grep` | Log parsing | `which grep` | Built-in to BusyBox |
| `awk` | Data processing | `which awk` | Built-in to BusyBox |
| `cat` | Read files | `which cat` | Built-in to BusyBox |
| `du` | Calculate data size | `which du` | Built-in to BusyBox |
| `journalctl` | Read systemd logs | `which journalctl` | Usually present in OpenBMC |
| `systemctl` | Control services | `which systemctl` | Usually present in OpenBMC |

### 2. NICE TO HAVE (Improves accuracy)

| Tool | Purpose | Check Command | Alternative if Missing |
|------|---------|---------------|------------------------|
| `pgrep` | Find process IDs | `which pgrep` | Use `ps aux \| grep` |
| `top` | CPU monitoring | `which top` | Use `ps aux` |
| `sleep` | Timing intervals | `which sleep` | Built-in to BusyBox |
| `kill` | Stop monitors | `which kill` | Built-in to BusyBox |
| `mkdir` | Create directories | `which mkdir` | Built-in to BusyBox |

### 3. NOT NEEDED (We'll use alternatives)

| Tool | Why Not Needed | Alternative |
|------|----------------|-------------|
| `bc` | Floating-point math | Use `awk` for calculations |
| `iostat` | Disk I/O stats | Read `/sys/block/*/stat` directly |
| `sar` | System activity | Read `/proc/stat` directly |
| `vmstat` | Virtual memory | Read `/proc/meminfo` directly |
| `ifstat` | Network stats | Read `/proc/net/dev` directly |
| `pidstat` | Process stats | Read `/proc/[pid]/stat` directly |

---

## Verification Script

Run this on your BMC to check what's available:

```bash
#!/bin/sh
echo "=== Checking Required Tools ==="
echo ""

# Critical tools
echo "CRITICAL TOOLS:"
for tool in date ps grep awk cat du journalctl systemctl; do
  if which $tool >/dev/null 2>&1; then
    echo "✓ $tool - FOUND"
  else
    echo "✗ $tool - MISSING (CRITICAL!)"
  fi
done

echo ""
echo "NICE-TO-HAVE TOOLS:"
for tool in pgrep top sleep kill mkdir; do
  if which $tool >/dev/null 2>&1; then
    echo "✓ $tool - FOUND"
  else
    echo "✗ $tool - MISSING (can work around)"
  fi
done

echo ""
echo "=== Checking /proc filesystem ==="
for path in /proc/stat /proc/net/dev /proc/diskstats /proc/meminfo; do
  if [ -r "$path" ]; then
    echo "✓ $path - READABLE"
  else
    echo "✗ $path - NOT READABLE"
  fi
done

echo ""
echo "=== Checking /sys filesystem ==="
# Check for common block devices
for device in mmcblk0 sda nvme0n1; do
  if [ -r "/sys/block/$device/stat" ]; then
    echo "✓ /sys/block/$device/stat - FOUND"
  fi
done

echo ""
echo "=== BusyBox Version ==="
busybox --help 2>&1 | head -1

echo ""
echo "=== Available BusyBox Applets ==="
busybox --list | sort
```

---

## Minimal Requirements Summary

### Absolutely Required:
1. **BusyBox** with these applets:
   - `date`, `ps`, `grep`, `awk`, `cat`, `du`, `sleep`, `kill`, `mkdir`

2. **Systemd tools** (standard in OpenBMC):
   - `journalctl`
   - `systemctl`

3. **Filesystem access**:
   - `/proc/stat` (CPU stats)
   - `/proc/net/dev` (network stats)
   - `/sys/block/*/stat` OR `/proc/diskstats` (disk I/O)

### Optional but Helpful:
- `pgrep` (can use `ps | grep` instead)
- `top` (can use `ps` instead)

---

## What to Install if Missing

### If `journalctl` is missing:
```bash
# This is unusual for OpenBMC, but if missing:
# You'll need to parse /var/log/messages or service logs directly
# Alternative: Use dmesg or direct log file parsing
```

### If basic BusyBox tools are missing:
```bash
# Check BusyBox configuration
busybox --list

# If a tool is missing from BusyBox, you may need to:
# 1. Rebuild BusyBox with that applet enabled, OR
# 2. Install standalone version (if space permits)
```

### If `/proc` or `/sys` are not accessible:
```bash
# This would be a serious system issue
# These should be mounted by default:
mount | grep proc
mount | grep sys
```

---

## Recommended: Run This First

```bash
# Save this as check_tools.sh and run on BMC
cat > /tmp/check_tools.sh << 'EOF'
#!/bin/sh
echo "Checking tools for performance testing..."

MISSING=""
for tool in date ps grep awk cat du journalctl systemctl; do
  if ! which $tool >/dev/null 2>&1; then
    MISSING="$MISSING $tool"
  fi
done

if [ -z "$MISSING" ]; then
  echo "✓ All required tools are available!"
  exit 0
else
  echo "✗ Missing tools:$MISSING"
  echo "Please install or enable these tools."
  exit 1
fi
EOF

chmod +x /tmp/check_tools.sh
/tmp/check_tools.sh
```

---

## Expected BusyBox Configuration

Most OpenBMC systems include these BusyBox applets by default:
- Shell utilities: `sh`, `ash`, `echo`, `test`, `[`
- File utilities: `cat`, `cp`, `mv`, `rm`, `ls`, `mkdir`, `touch`
- Text processing: `grep`, `sed`, `awk`, `cut`, `sort`, `uniq`
- Process management: `ps`, `kill`, `killall`, `top`
- System utilities: `date`, `sleep`, `time`, `uptime`
- Network utilities: `ping`, `wget`, `nc`

---

## Installation Commands (if needed)

### For OpenBMC/Yocto-based systems:
```bash
# Usually tools are already present
# If not, you may need to rebuild the image with required packages

# Check available packages
opkg list | grep busybox

# Update package list
opkg update

# Install if somehow missing (rare)
opkg install busybox
```

### For custom BusyBox builds:
```bash
# You'll need to rebuild BusyBox with required applets enabled
# Edit .config and enable:
# CONFIG_DATE=y
# CONFIG_PS=y
# CONFIG_GREP=y
# CONFIG_AWK=y
# etc.
```

---

## Bottom Line

**You likely don't need to install anything!**

Standard OpenBMC systems already have:
- ✓ BusyBox with all required applets
- ✓ systemd (journalctl, systemctl)
- ✓ /proc and /sys filesystems

**Just run the verification script above to confirm.**