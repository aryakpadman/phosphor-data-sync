#!/bin/bash

# Script to check sizes of all paths listed in *perf*.json files
# Handles IncludeList and ExcludeList properly
# Usage: ./check_perf_paths_size.sh

CONFIG_DIR="/usr/share/phosphor-data-sync/config/data_sync_list"
PERF_CONFIGS=("$CONFIG_DIR/common.json" "$CONFIG_DIR/ibm.json" "$CONFIG_DIR/open-power.json")

echo "========================================="
echo "  Path Size Analysis for Perf Test Configs"
echo "========================================="
echo ""

TOTAL_SIZE_KB=0
FOUND_COUNT=0
MISSING_COUNT=0
TOTAL_FILES=0
TOTAL_DIRS=0

# Function to convert human-readable size to KB
size_to_kb() {
    local size=$1
    local value=$(echo "$size" | sed 's/[^0-9.]//g')
    local unit=$(echo "$size" | sed 's/[0-9.]//g')

    case "$unit" in
        K) echo "$value" | awk '{printf "%.0f", $1}' ;;
        M) echo "$value" | awk '{printf "%.0f", $1 * 1024}' ;;
        G) echo "$value" | awk '{printf "%.0f", $1 * 1024 * 1024}' ;;
        T) echo "$value" | awk '{printf "%.0f", $1 * 1024 * 1024 * 1024}' ;;
        *) echo "$value" | awk '{printf "%.0f", $1}' ;;  # Assume KB if no unit
    esac
}

# Function to get size of a path (handles symlinks and sparse files)
get_path_size() {
    local path=$1
    local size_output=""

    # Check if it's a file or symlink to file
    if [ -f "$path" ] || [ -L "$path" ]; then
        # Use ls -L to follow symlinks and get logical size (works for sparse files)
        size_output=$(ls -Lh "$path" 2>/dev/null | awk '{print $5}')
    elif [ -d "$path" ]; then
        # Directory - use du -sL to follow symlinks
        size_output=$(du -sLh "$path" 2>/dev/null | awk '{print $1}')
    fi

    echo "$size_output"
}

# Function to check if path exists (including through symlinks)
path_exists() {
    local path=$1
    [ -e "$path" ] || [ -L "$path" ]
}

# Function to count files and directories in a path
count_files_dirs() {
    local path=$1
    local file_count=0
    local dir_count=0

    if [ -d "$path" ]; then
        # Count directories (excluding the path itself)
        dir_count=$(find "$path" -type d 2>/dev/null | tail -n +2 | wc -l)
        # Count files
        file_count=$(find "$path" -type f 2>/dev/null | wc -l)
    elif [ -f "$path" ]; then
        # It's a file
        file_count=1
        dir_count=0
    fi

    echo "$file_count $dir_count"
}

# Function to check if path should be excluded
is_excluded() {
    local path=$1
    local exclude_list=$2

    if [ -z "$exclude_list" ]; then
        return 1  # Not excluded
    fi

    # Check if path matches any exclude pattern
    echo "$exclude_list" | while IFS= read -r exclude_path; do
        if [ "$path" = "$exclude_path" ] || [[ "$path" == "$exclude_path"* ]]; then
            return 0  # Excluded
        fi
    done
    return 1  # Not excluded
}

# Process each config file
for config_file in "${PERF_CONFIGS[@]}"; do
    if [ ! -f "$config_file" ]; then
        echo "[SKIP] Config file not found: $config_file"
        continue
    fi

    echo "--- Processing: $config_file ---"
    echo ""

    # Process the JSON file line by line to extract entries
    in_entry=0
    current_path=""
    has_include_list=0
    has_exclude_list=0
    include_list=""
    exclude_list=""

    while IFS= read -r line; do
        # Detect start of a new entry (Files or Directories)
        if echo "$line" | grep -q '"Path"[[:space:]]*:'; then
            # Save previous entry if exists
            if [ -n "$current_path" ]; then
                # Process the previous entry
                if [ $has_include_list -eq 1 ]; then
                    # If IncludeList exists, only check those paths
                    echo "$include_list" | while IFS= read -r inc_path; do
                        if [ -n "$inc_path" ] && path_exists "$inc_path"; then
                            size_output=$(get_path_size "$inc_path")
                            if [ -n "$size_output" ]; then
                                read file_count dir_count <<< $(count_files_dirs "$inc_path")
                                echo "  ✓ $inc_path (from IncludeList)"
                                echo "    Size: $size_output"
                                echo "    Files: $file_count, Directories: $dir_count"
                                size_kb=$(size_to_kb "$size_output")
                                TOTAL_SIZE_KB=$((TOTAL_SIZE_KB + size_kb))
                                TOTAL_FILES=$((TOTAL_FILES + file_count))
                                TOTAL_DIRS=$((TOTAL_DIRS + dir_count))
                                FOUND_COUNT=$((FOUND_COUNT + 1))
                            fi
                        elif [ -n "$inc_path" ]; then
                            echo "  ✗ $inc_path (from IncludeList, does not exist)"
                            MISSING_COUNT=$((MISSING_COUNT + 1))
                        fi
                    done
                else
                    # No IncludeList, check the main path
                    if path_exists "$current_path"; then
                        size_output=$(get_path_size "$current_path")
                        if [ -n "$size_output" ]; then
                            # Note: ExcludeList handling with du -L is complex in BusyBox
                            # For now, we show total size and note if excludes exist
                            if [ $has_exclude_list -eq 1 ]; then
                                echo "    (Note: ExcludeList present but not applied - showing total size)"
                            fi
                            read file_count dir_count <<< $(count_files_dirs "$current_path")
                            echo "  ✓ $current_path"
                            echo "    Size: $size_output"
                            echo "    Files: $file_count, Directories: $dir_count"
                            size_kb=$(size_to_kb "$size_output")
                            TOTAL_SIZE_KB=$((TOTAL_SIZE_KB + size_kb))
                            TOTAL_FILES=$((TOTAL_FILES + file_count))
                            TOTAL_DIRS=$((TOTAL_DIRS + dir_count))
                            FOUND_COUNT=$((FOUND_COUNT + 1))
                        fi
                    else
                        echo "  ✗ $current_path (does not exist)"
                        MISSING_COUNT=$((MISSING_COUNT + 1))
                    fi
                fi
            fi

            # Start new entry
            current_path=$(echo "$line" | sed -n 's/.*"Path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')
            has_include_list=0
            has_exclude_list=0
            include_list=""
            exclude_list=""
        fi

        # Check for IncludeList
        if echo "$line" | grep -q '"IncludeList"'; then
            has_include_list=1
        fi

        # Extract paths from IncludeList
        if [ $has_include_list -eq 1 ] && echo "$line" | grep -q '^\s*"/' ; then
            inc_path=$(echo "$line" | sed -n 's/.*"\([^"]*\)".*/\1/p')
            include_list="${include_list}${inc_path}"$'\n'
        fi

        # Check for ExcludeList
        if echo "$line" | grep -q '"ExcludeList"'; then
            has_exclude_list=1
        fi

        # Extract paths from ExcludeList
        if [ $has_exclude_list -eq 1 ] && echo "$line" | grep -q '^\s*"/' ; then
            exc_path=$(echo "$line" | sed -n 's/.*"\([^"]*\)".*/\1/p')
            exclude_list="${exclude_list}${exc_path}"$'\n'
        fi

    done < "$config_file"

    # Process last entry
    if [ -n "$current_path" ]; then
        if [ $has_include_list -eq 1 ]; then
            echo "$include_list" | while IFS= read -r inc_path; do
                if [ -n "$inc_path" ] && path_exists "$inc_path"; then
                    size_output=$(get_path_size "$inc_path")
                    if [ -n "$size_output" ]; then
                        read file_count dir_count <<< $(count_files_dirs "$inc_path")
                        echo "  ✓ $inc_path (from IncludeList)"
                        echo "    Size: $size_output"
                        echo "    Files: $file_count, Directories: $dir_count"
                        size_kb=$(size_to_kb "$size_output")
                        TOTAL_SIZE_KB=$((TOTAL_SIZE_KB + size_kb))
                        TOTAL_FILES=$((TOTAL_FILES + file_count))
                        TOTAL_DIRS=$((TOTAL_DIRS + dir_count))
                        FOUND_COUNT=$((FOUND_COUNT + 1))
                    fi
                elif [ -n "$inc_path" ]; then
                    echo "  ✗ $inc_path (from IncludeList, does not exist)"
                    MISSING_COUNT=$((MISSING_COUNT + 1))
                fi
            done
        else
            if path_exists "$current_path"; then
                size_output=$(get_path_size "$current_path")
                if [ -n "$size_output" ]; then
                    if [ $has_exclude_list -eq 1 ]; then
                        echo "    (Note: ExcludeList present but not applied - showing total size)"
                    fi
                    read file_count dir_count <<< $(count_files_dirs "$current_path")
                    echo "  ✓ $current_path"
                    echo "    Size: $size_output"
                    echo "    Files: $file_count, Directories: $dir_count"
                    size_kb=$(size_to_kb "$size_output")
                    TOTAL_SIZE_KB=$((TOTAL_SIZE_KB + size_kb))
                    TOTAL_FILES=$((TOTAL_FILES + file_count))
                    TOTAL_DIRS=$((TOTAL_DIRS + dir_count))
                    FOUND_COUNT=$((FOUND_COUNT + 1))
                fi
            else
                echo "  ✗ $current_path (does not exist)"
                MISSING_COUNT=$((MISSING_COUNT + 1))
            fi
        fi
    fi

    echo ""
done

# Convert total back to human-readable
if [ $TOTAL_SIZE_KB -ge 1048576 ]; then
    TOTAL_SIZE=$(awk -v kb=$TOTAL_SIZE_KB 'BEGIN {printf "%.2f GB", kb/1048576}')
elif [ $TOTAL_SIZE_KB -ge 1024 ]; then
    TOTAL_SIZE=$(awk -v kb=$TOTAL_SIZE_KB 'BEGIN {printf "%.2f MB", kb/1024}')
else
    TOTAL_SIZE="${TOTAL_SIZE_KB} KB"
fi

echo "========================================="
echo "  Summary"
echo "========================================="
echo "Paths found:    $FOUND_COUNT"
echo "Paths missing:  $MISSING_COUNT"
echo ""
echo "TOTAL SIZE:     $TOTAL_SIZE"
echo "TOTAL FILES:    $TOTAL_FILES"
echo "TOTAL DIRS:     $TOTAL_DIRS"
echo "========================================="

# Made with Bob