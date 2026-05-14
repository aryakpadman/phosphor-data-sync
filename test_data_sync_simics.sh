#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Automated test script for phosphor-data-sync in Simics environment
# Tests all data-sync scenarios: Immediate, Periodic, and Full sync
#
# Usage: ./test_data_sync_simics.sh <hostname> <bmc0_port> <bmc1_port> <username> <password>
# Example: ./test_data_sync_simics.sh localhost 2028 2029 service 0penBmc

set -e

#==============================================================================
# PARSE COMMAND LINE ARGUMENTS
#==============================================================================
if [ $# -ne 5 ]; then
    echo "Usage: $0 <hostname> <bmc0_port> <bmc1_port> <username> <password>"
    echo ""
    echo "Arguments:"
    echo "  hostname   - BMC hostname or IP address"
    echo "  bmc0_port  - SSH port for BMC0 (e.g., 2028)"
    echo "  bmc1_port  - SSH port for BMC1 (e.g., 2029)"
    echo "  username   - SSH username (e.g., service)"
    echo "  password   - SSH password"
    echo ""
    echo "Example:"
    echo "  $0 localhost 2028 2029 service 0penBmc"
    exit 1
fi

BMC_HOST="$1"
BMC0_PORT="$2"
BMC1_PORT="$3"
BMC_USER="$4"
BMC_PASS="$5"

# Test configuration
DATA_SYNC_SERVICE="xyz.openbmc_project.Control.SyncBMCData.service"
BMC_REDUNDANCY_SERVICE="xyz.openbmc_project.State.BMC.Redundancy"
BMC_REDUNDANCY_INTERFACE="xyz.openbmc_project.State.BMC.Redundancy"
BMC0_REDUNDANCY_PATH="/xyz/openbmc_project/state/bmc0"
BMC1_REDUNDANCY_PATH="/xyz/openbmc_project/state/bmc1"

# Test paths
TEST_BASE_DIR="/var/tmp/data-sync-test"
TEST_FILE_PATH="${TEST_BASE_DIR}/a2p/immediate/file"
TEST_DIR_PATH="${TEST_BASE_DIR}/bidir/immediate/dir/"

# Timing configuration
FULL_SYNC_WAIT=5
IMMEDIATE_SYNC_WAIT=3
PERIODIC_SYNC_WAIT=2

#==============================================================================
# COLOR CODES
#==============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

#==============================================================================
# LOGGING FUNCTIONS
#==============================================================================
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_test() {
    echo -e "${CYAN}[TEST]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[✓ PASS]${NC} $1"
}

log_fail() {
    echo -e "${RED}[✗ FAIL]${NC} $1"
}

#==============================================================================
# SSH HELPER FUNCTIONS
#==============================================================================
# Execute command on BMC0
ssh_bmc0() {
    sshpass -p "${BMC_PASS}" ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p ${BMC0_PORT} ${BMC_USER}@${BMC_HOST} "$@" 2>/dev/null
}

# Execute command on BMC1
ssh_bmc1() {
    sshpass -p "${BMC_PASS}" ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p ${BMC1_PORT} ${BMC_USER}@${BMC_HOST} "$@" 2>/dev/null
}

# Copy file to BMC0
scp_to_bmc0() {
    local src=$1
    local dest=$2
    sshpass -p "${BMC_PASS}" scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -P ${BMC0_PORT} "${src}" ${BMC_USER}@${BMC_HOST}:"${dest}" 2>/dev/null
}

# Copy file to BMC1
scp_to_bmc1() {
    local src=$1
    local dest=$2
    sshpass -p "${BMC_PASS}" scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -P ${BMC1_PORT} "${src}" ${BMC_USER}@${BMC_HOST}:"${dest}" 2>/dev/null
}

# Copy file from BMC0
scp_from_bmc0() {
    local src=$1
    local dest=$2
    sshpass -p "${BMC_PASS}" scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -P ${BMC0_PORT} ${BMC_USER}@${BMC_HOST}:"${src}" "${dest}" 2>/dev/null
}

# Copy file from BMC1
scp_from_bmc1() {
    local src=$1
    local dest=$2
    sshpass -p "${BMC_PASS}" scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -P ${BMC1_PORT} ${BMC_USER}@${BMC_HOST}:"${src}" "${dest}" 2>/dev/null
}

#==============================================================================
# BMC ROLE DETECTION
#==============================================================================
get_bmc_role() {
    local bmc_num=$1
    local bmc_path="/xyz/openbmc_project/state/bmc${bmc_num}"

    if [ "$bmc_num" == "0" ]; then
        local role=$(ssh_bmc0 "busctl get-property ${BMC_REDUNDANCY_SERVICE} ${bmc_path} ${BMC_REDUNDANCY_INTERFACE} Role 2>/dev/null | awk '{print \$2}' | tr -d '\"'")
    else
        local role=$(ssh_bmc1 "busctl get-property ${BMC_REDUNDANCY_SERVICE} ${bmc_path} ${BMC_REDUNDANCY_INTERFACE} Role 2>/dev/null | awk '{print \$2}' | tr -d '\"'")
    fi

    echo "$role"
}

determine_bmc_roles() {
    log_info "Determining BMC roles..."

    BMC0_ROLE=$(get_bmc_role 0)
    BMC1_ROLE=$(get_bmc_role 1)

    log_info "BMC0 Role: ${BMC0_ROLE}"
    log_info "BMC1 Role: ${BMC1_ROLE}"

    if [[ "${BMC0_ROLE}" == *"Active"* ]]; then
        ACTIVE_BMC="BMC0"
        PASSIVE_BMC="BMC1"
        ACTIVE_PORT=${BMC0_PORT}
        PASSIVE_PORT=${BMC1_PORT}
    elif [[ "${BMC1_ROLE}" == *"Active"* ]]; then
        ACTIVE_BMC="BMC1"
        PASSIVE_BMC="BMC0"
        ACTIVE_PORT=${BMC1_PORT}
        PASSIVE_PORT=${BMC0_PORT}
    else
        log_error "Could not determine active BMC"
        return 1
    fi

    log_info "Active BMC: ${ACTIVE_BMC} (port ${ACTIVE_PORT})"
    log_info "Passive BMC: ${PASSIVE_BMC} (port ${PASSIVE_PORT})"
}

# Execute command on active BMC
ssh_active() {
    if [ "${ACTIVE_BMC}" == "BMC0" ]; then
        ssh_bmc0 "$@"
    else
        ssh_bmc1 "$@"
    fi
}

# Execute command on passive BMC
ssh_passive() {
    if [ "${PASSIVE_BMC}" == "BMC0" ]; then
        ssh_bmc0 "$@"
    else
        ssh_bmc1 "$@"
    fi
}

#==============================================================================
# UTILITY FUNCTIONS
#==============================================================================
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check if sshpass is installed
    if ! command -v sshpass &> /dev/null; then
        log_error "sshpass is not installed. Please install it: sudo apt-get install sshpass"
        exit 1
    fi

    # Test SSH connectivity to both BMCs
    if ! ssh_bmc0 "echo 'BMC0 connection OK'" &> /dev/null; then
        log_error "Cannot connect to BMC0 on port ${BMC0_PORT}"
        exit 1
    fi
    log_info "BMC0 connection: OK"

    if ! ssh_bmc1 "echo 'BMC1 connection OK'" &> /dev/null; then
        log_error "Cannot connect to BMC1 on port ${BMC1_PORT}"
        exit 1
    fi
    log_info "BMC1 connection: OK"

    # Check if data-sync service exists on both BMCs
    if ! ssh_bmc0 "systemctl list-unit-files | grep -q ${DATA_SYNC_SERVICE}"; then
        log_error "Data-sync service not found on BMC0"
        exit 1
    fi

    if ! ssh_bmc1 "systemctl list-unit-files | grep -q ${DATA_SYNC_SERVICE}"; then
        log_error "Data-sync service not found on BMC1"
        exit 1
    fi
    log_info "Data-sync service found on both BMCs"
}

restart_data_sync_service() {
    local bmc=$1
    log_info "Restarting data-sync service on ${bmc}..."

    if [ "${bmc}" == "BMC0" ]; then
        ssh_bmc0 "systemctl restart ${DATA_SYNC_SERVICE}"
        sleep 2

        # Show ALL journal logs (not filtered by service)
        log_info "=== Journal logs from ${bmc} after restart ==="
        ssh_bmc0 "journalctl --no-pager -n 50 --since '10 seconds ago'"
        log_info "=== End of journal logs ==="
    else
        ssh_bmc1 "systemctl restart ${DATA_SYNC_SERVICE}"
        sleep 2

        # Show ALL journal logs (not filtered by service)
        log_info "=== Journal logs from ${bmc} after restart ==="
        ssh_bmc1 "journalctl --no-pager -n 50 --since '10 seconds ago'"
        log_info "=== End of journal logs ==="
    fi

    # Verify service is running
    if [ "${bmc}" == "BMC0" ]; then
        if ssh_bmc0 "systemctl is-active --quiet ${DATA_SYNC_SERVICE}"; then
            log_info "✓ Data-sync service is ACTIVE on ${bmc}"
            return 0
        fi
    else
        if ssh_bmc1 "systemctl is-active --quiet ${DATA_SYNC_SERVICE}"; then
            log_info "✓ Data-sync service is ACTIVE on ${bmc}"
            return 0
        fi
    fi

    log_error "Data-sync service failed to start on ${bmc}"
    return 1
}

# Function to show journal logs during sync operations
show_sync_logs() {
    local bmc=$1
    local message=$2

    log_info "${message}"
    log_info "=== Recent journal activity on ${bmc} ==="
    if [ "${bmc}" == "BMC0" ]; then
        ssh_bmc0 "journalctl --no-pager -n 30 --since '5 seconds ago'"
    else
        ssh_bmc1 "journalctl --no-pager -n 30 --since '5 seconds ago'"
    fi
    log_info "=== End of journal logs ==="
}

compare_files() {
    local file_path=$1
    local temp_dir=$(mktemp -d)
    local active_file="${temp_dir}/active_file"
    local passive_file="${temp_dir}/passive_file"

    # Download files from both BMCs
    if [ "${ACTIVE_BMC}" == "BMC0" ]; then
        scp_from_bmc0 "${file_path}" "${active_file}" 2>/dev/null || return 1
        scp_from_bmc1 "${file_path}" "${passive_file}" 2>/dev/null || return 1
    else
        scp_from_bmc1 "${file_path}" "${active_file}" 2>/dev/null || return 1
        scp_from_bmc0 "${file_path}" "${passive_file}" 2>/dev/null || return 1
    fi

    # Compare using MD5
    local active_md5=$(md5sum "${active_file}" | awk '{print $1}')
    local passive_md5=$(md5sum "${passive_file}" | awk '{print $1}')

    rm -rf "${temp_dir}"

    if [ "${active_md5}" == "${passive_md5}" ]; then
        log_info "Files match: ${file_path}"
        log_info "  MD5: ${active_md5}"
        return 0
    else
        log_error "Files differ: ${file_path}"
        log_error "  Active MD5:  ${active_md5}"
        log_error "  Passive MD5: ${passive_md5}"
        return 1
    fi
}

cleanup_test_files() {
    log_info "Cleaning up test files..."
    ssh_bmc0 "rm -rf ${TEST_BASE_DIR}" 2>/dev/null || true
    ssh_bmc1 "rm -rf ${TEST_BASE_DIR}" 2>/dev/null || true
}

#==============================================================================
# TEST CASES - IMMEDIATE SYNC
#==============================================================================

# Test 1: Update existing file (Active2Passive)
test_update_on_existing_file() {
    log_test "TEST 1: Update existing file (Immediate Sync - Active2Passive)"

    local test_file="${TEST_BASE_DIR}/immediate/test_file"

    # Create directory structure on active BMC
    ssh_active "mkdir -p $(dirname ${test_file})"

    # Create initial file
    local initial_data="Initial data - $(date +%s)"
    ssh_active "echo '${initial_data}' > ${test_file}"

    # Verify file created
    if ! ssh_active "test -f ${test_file}"; then
        log_fail "Failed to create test file on active BMC"
        return 1
    fi

    # Restart data-sync service to trigger full sync
    restart_data_sync_service "${ACTIVE_BMC}"

    # Wait for full sync
    log_info "Waiting ${FULL_SYNC_WAIT}s for full sync..."
    sleep ${FULL_SYNC_WAIT}

    # Modify the file to trigger immediate sync
    local updated_data="Updated data - $(date +%s) - ${RANDOM}"
    ssh_active "echo '${updated_data}' > ${test_file}"
    log_info "File modified with: ${updated_data}"

    # Wait for immediate sync
    log_info "Waiting ${IMMEDIATE_SYNC_WAIT}s for immediate sync..."
    sleep ${IMMEDIATE_SYNC_WAIT}

    # Show sync logs from active BMC
    show_sync_logs "${ACTIVE_BMC}" "Sync logs from ${ACTIVE_BMC} after file modification:"

    # Verify file exists on passive BMC
    if ! ssh_passive "test -f ${test_file}"; then
        log_fail "File not found on passive BMC"
        return 1
    fi

    # Compare files
    if compare_files "${test_file}"; then
        log_pass "File synced successfully"
        return 0
    else
        log_fail "File content mismatch"
        return 1
    fi
}

# Test 2: Delete file in directory (Immediate Sync)
test_delete_file_in_directory() {
    log_test "TEST 2: Delete file in directory (Immediate Sync)"

    local test_dir="${TEST_BASE_DIR}/immediate/test_dir/"
    local test_file="${test_dir}test_file"

    # Create directory and file on active BMC
    ssh_active "mkdir -p ${test_dir}"
    ssh_active "echo 'Test data' > ${test_file}"

    # Create same structure on passive BMC
    ssh_passive "mkdir -p ${test_dir}"
    ssh_passive "echo 'Test data' > ${test_file}"

    # Restart service
    restart_data_sync_service "${ACTIVE_BMC}"
    sleep ${FULL_SYNC_WAIT}

    # Delete file on active BMC
    ssh_active "rm -f ${test_file}"
    log_info "File deleted on active BMC"

    # Wait for sync
    sleep ${IMMEDIATE_SYNC_WAIT}

    # Show sync logs
    show_sync_logs "${ACTIVE_BMC}" "Sync logs from ${ACTIVE_BMC} after file deletion:"

    # Verify file deleted on passive BMC
    if ssh_passive "test -f ${test_file}"; then
        log_fail "File still exists on passive BMC"
        return 1
    else
        log_pass "File deletion synced successfully"
        return 0
    fi
}

# Test 3: Create new file (Immediate Sync)
test_create_new_file() {
    log_test "TEST 3: Create new file (Immediate Sync)"

    local test_dir="${TEST_BASE_DIR}/immediate/new_file_dir/"
    local test_file="${test_dir}new_file"

    # Create directory on both BMCs
    ssh_active "mkdir -p ${test_dir}"
    ssh_passive "mkdir -p ${test_dir}"

    # Restart service
    restart_data_sync_service "${ACTIVE_BMC}"
    sleep ${FULL_SYNC_WAIT}

    # Create new file on active BMC
    local data="New file data - $(date +%s)"
    ssh_active "echo '${data}' > ${test_file}"
    log_info "New file created on active BMC"

    # Wait for sync
    sleep ${IMMEDIATE_SYNC_WAIT}

    # Show sync logs
    show_sync_logs "${ACTIVE_BMC}" "Sync logs from ${ACTIVE_BMC} after file creation:"

    # Verify file exists on passive BMC
    if ! ssh_passive "test -f ${test_file}"; then
        log_fail "New file not found on passive BMC"
        return 1
    fi

    # Compare files
    if compare_files "${test_file}"; then
        log_pass "New file synced successfully"
        return 0
    else
        log_fail "New file content mismatch"
        return 1
    fi
}

# Test 4: Create subdirectory (Immediate Sync)
test_create_subdirectory() {
    log_test "TEST 4: Create subdirectory (Immediate Sync)"

    local test_dir="${TEST_BASE_DIR}/immediate/parent_dir/"
    local sub_dir="${test_dir}sub_dir/"
    local test_file="${sub_dir}file"

    # Create parent directory on both BMCs
    ssh_active "mkdir -p ${test_dir}"
    ssh_passive "mkdir -p ${test_dir}"

    # Restart service
    restart_data_sync_service "${ACTIVE_BMC}"
    sleep ${FULL_SYNC_WAIT}

    # Create subdirectory with file on active BMC
    ssh_active "mkdir -p ${sub_dir}"
    ssh_active "echo 'Subdir file' > ${test_file}"
    log_info "Subdirectory created on active BMC"

    # Wait for sync
    sleep ${IMMEDIATE_SYNC_WAIT}

    # Show sync logs
    show_sync_logs "${ACTIVE_BMC}" "Sync logs from ${ACTIVE_BMC} after subdirectory creation:"

    # Verify subdirectory and file exist on passive BMC
    if ! ssh_passive "test -d ${sub_dir}"; then
        log_fail "Subdirectory not found on passive BMC"
        return 1
    fi

    if ! ssh_passive "test -f ${test_file}"; then
        log_fail "File in subdirectory not found on passive BMC"
        return 1
    fi

    log_pass "Subdirectory synced successfully"
    return 0
}

#==============================================================================
# TEST EXECUTION
#==============================================================================

# Test results tracking
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

run_test() {
    local test_name=$1
    local test_func=$2

    echo ""
    echo "========================================="
    log_test "Running: ${test_name}"
    echo "========================================="

    TESTS_RUN=$((TESTS_RUN + 1))

    # Clean up before test
    cleanup_test_files

    # Run test
    if ${test_func}; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        echo ""
        log_pass "${test_name}"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        echo ""
        log_fail "${test_name}"
    fi

    # Clean up after test
    cleanup_test_files
}

print_summary() {
    echo ""
    echo "========================================="
    echo "           TEST SUMMARY"
    echo "========================================="
    echo -e "Total Tests:  ${TESTS_RUN}"
    echo -e "${GREEN}Passed:       ${TESTS_PASSED}${NC}"
    echo -e "${RED}Failed:       ${TESTS_FAILED}${NC}"
    echo "========================================="

    if [ ${TESTS_FAILED} -eq 0 ]; then
        echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
        return 0
    else
        echo -e "${RED}✗ SOME TESTS FAILED${NC}"
        return 1
    fi
}

#==============================================================================
# MAIN EXECUTION
#==============================================================================

main() {
    echo "========================================="
    echo "  Phosphor Data-Sync Automated Tests"
    echo "  Simics Environment"
    echo "========================================="
    echo ""

    # Check prerequisites
    check_prerequisites

    # Determine BMC roles
    if ! determine_bmc_roles; then
        log_error "Failed to determine BMC roles"
        exit 1
    fi

    echo ""
    log_info "Starting test execution..."
    echo ""

    # Run immediate sync tests
    run_test "Update existing file" test_update_on_existing_file
    run_test "Delete file in directory" test_delete_file_in_directory
    run_test "Create new file" test_create_new_file
    run_test "Create subdirectory" test_create_subdirectory

    # Print summary
    echo ""
    if print_summary; then
        exit 0
    else
        exit 1
    fi
}

# Run main
main "$@"

# Made with Bob

