#!/bin/sh
# Script to populate dummy test data for phosphor-data-sync testing on AST2700 BMC
# Compatible with busybox
# Usage: ./test_dummy_config.sh <json_file> {populate|cleanup}

set -e

# Function to extract paths from JSON file
extract_paths_from_json() {
    local json_file="$1"

    if [ ! -f "$json_file" ]; then
        echo "Error: JSON file '$json_file' not found!"
        exit 1
    fi

    # Extract file paths (simple grep/sed approach for busybox compatibility)
    # This extracts lines with "Path": and gets the path value
    grep -o '"Path"[[:space:]]*:[[:space:]]*"[^"]*"' "$json_file" | \
        sed 's/"Path"[[:space:]]*:[[:space:]]*"\([^"]*\)"/\1/g'
}

# Function to cleanup all test data based on JSON config
cleanup_test_data() {
    local json_file="$1"

    echo "Cleaning up test data from config: $json_file"
    echo "========================================"

    # Extract all paths from JSON
    local paths=$(extract_paths_from_json "$json_file")

    if [ -z "$paths" ]; then
        echo "No paths found in JSON file"
        return
    fi

    echo "Paths to be deleted:"
    local deleted_count=0

    # Remove each path
    for path in $paths; do
        if [ -e "$path" ]; then
            echo "  [DELETED] $path"
            rm -rf "$path"
            deleted_count=$((deleted_count + 1))
        else
            echo "  [SKIP] $path (does not exist)"
        fi
    done

    # Clean up empty parent directories
    echo ""
    echo "Cleaning up parent directory..."
    if [ -d /var/data-sync-test ]; then
        echo "  [DELETED] /var/data-sync-test/"
        rm -rf /var/data-sync-test 2>/dev/null || true
    fi

    echo ""
    echo "========================================"
    echo "Cleanup completed! Deleted $deleted_count path(s)"
}

# Function to populate test data based on JSON config
populate_test_data() {
    local json_file="$1"

    echo "Populating test data from config: $json_file"
    echo "========================================"

    # Extract all paths from JSON
    local paths=$(extract_paths_from_json "$json_file")

    if [ -z "$paths" ]; then
        echo "No paths found in JSON file"
        return
    fi

    echo "Files and directories to be created:"
    local created_count=0

    # Create each path
    for path in $paths; do
        # Remove trailing slash if present
        path=$(echo "$path" | sed 's:/$::')

        # Check if it's a directory (ends with / in original or is BMC0)
        if echo "$path" | grep -q "BMC0$"; then
            # It's a directory - create it and add test files
            echo "  [DIR] $path"
            mkdir -p "$path"
            created_count=$((created_count + 1))

            # Create timestamped file
            TIMESTAMP=$(date +%Y%m%d_%H%M%S)
            TIMESTAMP_FILE="$path/test_file_${TIMESTAMP}"
            echo "123 - Test data at ${TIMESTAMP}" > "$TIMESTAMP_FILE"
            echo "    [FILE] $TIMESTAMP_FILE"
            created_count=$((created_count + 1))

            # Create subdirectories with files
            mkdir -p "$path/subdir1"
            echo "456 - Data in subdir1" > "$path/subdir1/file1"
            echo "    [FILE] $path/subdir1/file1"
            created_count=$((created_count + 1))

            mkdir -p "$path/subdir2"
            echo "789 - Data in subdir2" > "$path/subdir2/file2"
            echo "    [FILE] $path/subdir2/file2"
            created_count=$((created_count + 1))
        else
            # It's a file - create parent directory and the file
            local dir=$(dirname "$path")
            mkdir -p "$dir"
            echo "123" > "$path"
            echo "  [FILE] $path"
            created_count=$((created_count + 1))
        fi
    done

    echo ""
    echo "========================================"
    echo "Population completed! Created $created_count item(s)"
}

# Main script logic
if [ $# -lt 1 ]; then
    echo "Usage: $0 <json_config_file> {populate|cleanup}"
    echo "  json_config_file - Path to JSON configuration file"
    echo "  populate         - Create test files and directories (default)"
    echo "  cleanup          - Remove all test files and directories"
    echo ""
    echo "Examples:"
    echo "  $0 automation/dummy_immediate_full_sync.json populate"
    echo "  $0 automation/dummy_periodic_full_sync.json cleanup"
    exit 1
fi

JSON_FILE="$1"
ACTION="${2:-populate}"

case "$ACTION" in
    populate)
        populate_test_data "$JSON_FILE"
        ;;
    cleanup)
        cleanup_test_data "$JSON_FILE"
        ;;
    *)
        echo "Error: Invalid action '$ACTION'"
        echo "Usage: $0 <json_config_file> {populate|cleanup}"
        exit 1
        ;;
esac

# Made with Bob
