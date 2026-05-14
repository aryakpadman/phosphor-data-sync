# Phosphor Data-Sync Automated Test Script

## Overview

This automated test script validates the phosphor-data-sync functionality in a Simics BMC environment. It tests immediate sync, periodic sync, and full sync scenarios by connecting to both BMC0 and BMC1 via SSH.

## Prerequisites

### 1. Install sshpass
The script uses `sshpass` for automated SSH authentication:

```bash
# Ubuntu/Debian
sudo apt-get install sshpass

# RHEL/CentOS
sudo yum install sshpass

# macOS (using Homebrew)
brew install hudochenkov/sshpass/sshpass
```

### 2. Simics Environment Setup
- Both BMC0 and BMC1 must be running in Simics
- SSH access must be enabled on both BMCs
- Data-sync service must be installed and configured on both BMCs
- BMC redundancy must be configured (one Active, one Passive)

### 3. Network Connectivity
- Ensure you can reach both BMCs from your host machine
- Default ports: BMC0 (2028), BMC1 (2029)

## Usage

### Basic Usage

```bash
chmod +x test_data_sync_simics.sh
./test_data_sync_simics.sh <hostname> <bmc0_port> <bmc1_port> <username> <password>
```

### Example

```bash
./test_data_sync_simics.sh localhost 2028 2029 service 0penBmc
```

### Arguments

| Argument | Description | Example |
|----------|-------------|---------|
| hostname | BMC hostname or IP address | `localhost` or `192.168.1.100` |
| bmc0_port | SSH port for BMC0 | `2028` |
| bmc1_port | SSH port for BMC1 | `2029` |
| username | SSH username for both BMCs | `service` or `root` |
| password | SSH password for both BMCs | `0penBmc` |

## Test Cases

The script currently implements the following test cases:

### Immediate Sync Tests

1. **Update Existing File (Active2Passive)**
   - Creates a file on the active BMC
   - Modifies the file to trigger immediate sync
   - Verifies the file is synced to the passive BMC
   - Compares file content using MD5 checksums

2. **Delete File in Directory**
   - Creates a file in a directory on both BMCs
   - Deletes the file on the active BMC
   - Verifies the file is deleted on the passive BMC

3. **Create New File**
   - Creates a new file on the active BMC
   - Verifies the file is synced to the passive BMC
   - Compares file content

4. **Create Subdirectory**
   - Creates a subdirectory with files on the active BMC
   - Verifies the subdirectory structure is synced to the passive BMC

## Test Execution Flow

For each test case, the script:

1. **Setup Phase**
   - Cleans up any existing test files
   - Creates necessary directory structures
   - Creates initial test files

2. **Sync Trigger Phase**
   - Restarts the data-sync service to trigger full sync
   - Waits for full sync to complete (default: 5 seconds)
   - Performs the test action (create/modify/delete)

3. **Verification Phase**
   - Waits for immediate sync to propagate (default: 3 seconds)
   - Verifies files exist/don't exist as expected
   - Compares file content using MD5 checksums

4. **Cleanup Phase**
   - Removes all test files from both BMCs

## Output

The script provides color-coded output:

- **Green**: Informational messages and passed tests
- **Red**: Errors and failed tests
- **Yellow**: Warnings
- **Cyan**: Test case names

### Example Output

```
=========================================
  Phosphor Data-Sync Automated Tests
  Simics Environment
=========================================

[INFO] Checking prerequisites...
[INFO] BMC0 connection: OK
[INFO] BMC1 connection: OK
[INFO] Data-sync service found on both BMCs
[INFO] Determining BMC roles...
[INFO] BMC0 Role: xyz.openbmc_project.State.BMC.Redundancy.Role.Active
[INFO] BMC1 Role: xyz.openbmc_project.State.BMC.Redundancy.Role.Passive
[INFO] Active BMC: BMC0 (port 2028)
[INFO] Passive BMC: BMC1 (port 2029)

[INFO] Starting test execution...

=========================================
[TEST] Running: Update existing file
=========================================
[TEST] TEST 1: Update existing file (Immediate Sync - Active2Passive)
[INFO] Restarting data-sync service on BMC0...
[INFO] Data-sync service running on BMC0
[INFO] Waiting 5s for full sync...
[INFO] File modified with: Updated data - 1743598234 - 12345
[INFO] Waiting 3s for immediate sync...
[INFO] Files match: /var/tmp/data-sync-test/immediate/test_file
[INFO]   MD5: a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
[✓ PASS] File synced successfully

[✓ PASS] Update existing file

=========================================
           TEST SUMMARY
=========================================
Total Tests:  4
Passed:       4
Failed:       0
=========================================
✓ ALL TESTS PASSED
```

## Configuration

### Timing Parameters

You can adjust timing parameters in the script:

```bash
FULL_SYNC_WAIT=5        # Seconds to wait for full sync
IMMEDIATE_SYNC_WAIT=3   # Seconds to wait for immediate sync
PERIODIC_SYNC_WAIT=2    # Seconds to wait for periodic sync
```

### Test Paths

Test files are created under:
```bash
TEST_BASE_DIR="/var/tmp/data-sync-test"
```

## Troubleshooting

### SSH Connection Issues

If you see "Cannot connect to BMC" errors:

1. Verify BMCs are running: `ping <hostname>`
2. Check SSH ports are accessible: `nc -zv <hostname> <port>`
3. Verify credentials are correct
4. Check firewall rules

### Service Not Found

If data-sync service is not found:

1. Verify service is installed: `systemctl list-unit-files | grep SyncBMCData`
2. Check service status: `systemctl status xyz.openbmc_project.Control.SyncBMCData.service`

### Sync Not Working

If files are not syncing:

1. Check data-sync service logs: `journalctl -u xyz.openbmc_project.Control.SyncBMCData.service -f`
2. Verify BMC redundancy is configured correctly
3. Check data-sync configuration files exist
4. Ensure debug mode is enabled if needed

### Test Failures

If tests fail:

1. Check the test output for specific error messages
2. Verify both BMCs have sufficient disk space
3. Check file permissions on test directories
4. Review data-sync service logs on both BMCs

## Extending the Script

To add new test cases:

1. Create a new test function following the naming convention `test_<name>()`
2. Add the test to the main execution section:
   ```bash
   run_test "Test description" test_<name>
   ```

Example:
```bash
test_my_new_test() {
    log_test "TEST: My new test case"

    # Test implementation
    # ...

    if [ success ]; then
        log_pass "Test passed"
        return 0
    else
        log_fail "Test failed"
        return 1
    fi
}

# In main():
run_test "My new test" test_my_new_test
```

## Security Notes

- The script uses `sshpass` which passes passwords in plain text
- For production environments, consider using SSH keys instead
- The script disables SSH host key checking for convenience
- Ensure the script is not committed with hardcoded credentials

## Support

For issues or questions:
- Check the phosphor-data-sync documentation
- Review the test output logs
- Check BMC system logs: `journalctl -xe`

## License

SPDX-License-Identifier: Apache-2.0