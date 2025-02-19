// SPDX-License-Identifier: Apache-2.0

#include "data_watcher.hpp"

#include <phosphor-logging/lg2.hpp>

#include <string>

namespace data_sync
{
namespace watch::inotify
{

void DataWatcher::printWD(const std::map<int, fs::path>& _watchDescriptors)
{
    for (const auto& [path, wd] : _watchDescriptors)
    {
        lg2::info("{PATH} --> {WD}", "PATH", path, "WD", wd);
    }
}
DataWatcher::DataWatcher(sdbusplus::async::context& ctx, const int inotifyFlags,
                         const uint32_t eventMasksToWatch,
                         const std::filesystem::path& dataPathToWatch) :
    _inotifyFlags(inotifyFlags), _eventMasksToWatch(eventMasksToWatch),
    _dataPathToWatch(dataPathToWatch), _inotifyFileDescriptor(inotifyInit()),
    _fdioInstance(
        std::make_unique<sdbusplus::async::fdio>(ctx, _inotifyFileDescriptor()))
{
    if (!std::filesystem::exists(_dataPathToWatch))
    {
        lg2::info("Given path [{PATH}] doesn't exist to watch", "PATH",
                  _dataPathToWatch);
        // TODO : Handle not exists sceanrio by monitoring parent
    }

    try
    {
        createWatchers(_dataPathToWatch, _eventMasksToWatch);
        lg2::info("Started watching the PATH : {PATH}", "PATH",
                  _dataPathToWatch);
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception : {EXCEPTION}", "EXCEPTION", e);
    }
}

// NOLINTNEXTLINE
DataWatcher::~DataWatcher()
{
    auto isWatchDescriptorsValid = [this]() {
        return std::ranges::all_of(
            _watchDescriptors, [](const auto& wd) { return wd.first >= 0; });
    };

    if ((_inotifyFileDescriptor() >= 0) &&
        (static_cast<int>(isWatchDescriptorsValid()) == 1))
    {
        std::ranges::for_each(_watchDescriptors, [this](const auto& wd) {
            inotify_rm_watch(_inotifyFileDescriptor(), wd.first);
        });
    }
}

int DataWatcher::inotifyInit() const
{
    auto fd = inotify_init1(_inotifyFlags);

    if (-1 == fd)
    {
        lg2::error("inotify_init1 call failed with ErrNo : {ERRNO}, ErrMsg : "
                   "{ERRMSG}",
                   "ERRNO", errno, "ERRMSG", strerror(errno));

        // TODO: Throw meaningful exception
        throw std::runtime_error("inotify_init1 failed");
    }
    return fd;
}

void DataWatcher::addToWatchList(const fs::path& currentPathToWatch,
                                 uint32_t eventMasksToWatch)
{
    int wd = -1;
    wd = inotify_add_watch(_inotifyFileDescriptor(), currentPathToWatch.c_str(),
                           eventMasksToWatch);
    if (-1 == wd)
    {
        lg2::error(
            "inotify_add_watch call failed for {PATH} with ErrNo : {ERRNO}, "
            "ErrMsg : {ERRMSG}",
            "PATH", currentPathToWatch, "ERRNO", errno, "ERRMSG",
            strerror(errno));
        // TODO: create error log ? bcoz not watching the  path
        throw std::runtime_error("Failed to add to watch list");
    }
    else
    {
        lg2::debug("Watch added. PATH : {PATH}, wd : {WD}", "PATH",
                   currentPathToWatch, "WD", wd);
        _watchDescriptors.insert({wd, currentPathToWatch});
        printWD(_watchDescriptors);
    }
}

void DataWatcher::createWatchers(const fs::path& currentPathToWatch,
                                 uint32_t eventMasksToWatch)
{
    auto currentPathToWatchExist = fs::exists(currentPathToWatch);
    if (currentPathToWatchExist)
    {
        addToWatchList(currentPathToWatch, eventMasksToWatch);
        /* Add watch for subdirectories also if path is a directory
         */
        if (fs::is_directory(currentPathToWatch))
        {
            auto addWatchIfDir = [this,
                                  &eventMasksToWatch](const fs::path& entry) {
                if (fs::is_directory(entry))
                {
                    addToWatchList(entry, eventMasksToWatch);
                    return true;
                }
                return false;
            };
            std::ranges::for_each(
                fs::recursive_directory_iterator(currentPathToWatch),
                addWatchIfDir);
        }
    }
    else
    {
        // TODO : If configured path not exist, monitor parent until it creates
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<bool> DataWatcher::onDataChange()
{
    // NOLINTNEXTLINE
    co_await _fdioInstance->next();

    std::optional<std::vector<eventInfo>> receivedEvents = readEvents();

    if (receivedEvents.has_value())
    {
        std::vector<bool> isSyncRequire{true};
        std::ranges::for_each(
            receivedEvents.value(),
            [this, &isSyncRequire](const auto& receivedEventInfo) {
            isSyncRequire.emplace_back(
                processReceivedEvents(receivedEventInfo));
        });

        co_return std::ranges::any_of(isSyncRequire,
                                      [](bool item) { return item; });
    }
    co_return false;
}

std::optional<std::vector<eventInfo>> DataWatcher::readEvents()
{
    // Maximum inotify events supported in the buffer
    constexpr auto maxBytes = sizeof(struct inotify_event) + NAME_MAX + 1;
    uint8_t buffer[maxBytes];

    auto bytes = read(_inotifyFileDescriptor(), buffer, maxBytes);
    if (0 > bytes)
    {
        // Failed to read inotify event
        lg2::error("Failed to read inotify event");
        return std::nullopt;
    }

    auto offset = 0;
    std::vector<eventInfo> receivedEvents{};
    while (offset < bytes)
    {
        // NOLINTNEXTLINE to avoid cppcoreguidelines-pro-type-reinterpret-cast
        auto* receivedEvent = reinterpret_cast<inotify_event*>(&buffer[offset]);

        receivedEvents.emplace_back(receivedEvent->wd, receivedEvent->name,
                                    receivedEvent->mask);

        lg2::debug("Received inotify event with mask : {MASK}", "MASK",
                   receivedEvent->mask);
        offset += offsetof(inotify_event, name) + receivedEvent->len;
    }
    return receivedEvents;
}

bool DataWatcher::processReceivedEvents(eventInfo receivedEventInfo)
{
    /**
     * Sync files upon modifications
     * Case 1 : Files listed in the config and are already existing
     * Case 2 : Files created inside a watching directory
     */
    if ((std::get<2>(receivedEventInfo) & IN_CLOSE_WRITE) != 0)
    {
        lg2::debug("Syncing {DATA}", "DATA", _dataPathToWatch);
        return true;
    }
    /**
     * To handle the creation of sub directories inside a monitoring DIR
     */
    else if (((std::get<2>(receivedEventInfo) & IN_CREATE) != 0) &&
             ((std::get<2>(receivedEventInfo) & IN_ISDIR) != 0))
    {
        // TODO : Add if check whether the IN_CREATE received for dir IN_ISDIR

        lg2::info("Syncing {DATA} for IN_CREATE", "DATA", _dataPathToWatch);

        // add watch for the created child subdirectories
        fs::path createdPath =
            _watchDescriptors.at(std::get<0>(receivedEventInfo)) /
            std::get<1>(receivedEventInfo);
        if (fs::is_directory(createdPath))
        {
            createWatchers(createdPath, _eventMasksToWatch);
        }
        return true;
    }
    return false;
}

} // namespace watch::inotify
} // namespace data_sync
