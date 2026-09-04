// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "utility.hpp"

#include <unistd.h>

#include <phosphor-logging/lg2.hpp>

#include <filesystem>
#include <regex>
#include <utility>

namespace data_sync::utility
{

namespace fs = std::filesystem;

FD::FD(int fd) : fd(fd) {}

FD::~FD()
{
    reset();
}

FD::FD(FD&& other) noexcept : fd(std::exchange(other.fd, -1)) {}

FD& FD::operator=(FD&& other) noexcept
{
    if (this != &other)
    {
        reset();
        fd = std::exchange(other.fd, -1);
    }

    return *this;
}

void FD::reset()
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

int FD::operator()() const
{
    return fd;
}

void setupPaths()
{
    const fs::path persistPath{"/var/lib/phosphor-data-sync/"};
    std::error_code ec;
    // Directory to keep the sibling BMC's data as backup on local BMC
    const fs::path bkpPath{persistPath / "bmc_data_bkp/"};
    if (!fs::exists(bkpPath))
    {
        if (!fs::create_directories(bkpPath, ec))
        {
            lg2::error("Failed to create the path[{PATH}] : Error : {ERROR}",
                       "PATH", bkpPath, "ERROR", ec.message());
            throw std::runtime_error("Failed to create the path: " +
                                     bkpPath.string());
        }
    }

    // Directory where sibling notify requests get created
    const fs::path notifySiblingDir{NOTIFY_SIBLING_DIR};
    if (!fs::exists(notifySiblingDir))
    {
        if (!fs::create_directories(notifySiblingDir, ec))
        {
            lg2::error("Failed to create the path[{PATH}] : Error : {ERROR}",
                       "PATH", notifySiblingDir, "ERROR", ec.message());
            throw std::runtime_error("Failed to create the path: " +
                                     notifySiblingDir.string());
        }
    }

    // Directory which receives the notify requests from sibling BMC
    const fs::path notifyServiceDir{NOTIFY_SERVICES_DIR};
    if (!fs::exists(notifyServiceDir))
    {
        if (!fs::create_directories(notifyServiceDir, ec))
        {
            lg2::error("Failed to create the path[{PATH}] : Error : {ERROR}",
                       "PATH", notifyServiceDir, "ERROR", ec.message());
            throw std::runtime_error("Failed to create the path: " +
                                     notifyServiceDir.string());
        }
    }
}

namespace rsync
{

bool isSynced(const std::string& rsyncOutput)
{
    // Match either:
    //   - "Literal data:" with a non-zero value (data written to remote)
    //   - "*deleting " line (file deleted on remote, via --itemize-changes)
    std::regex re(R"(Literal data:\s*[1-9]|\*deleting\s+)");
    return std::regex_search(rsyncOutput, re);
}

} // namespace rsync
} // namespace data_sync::utility
