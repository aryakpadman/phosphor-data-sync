// SPDX-License-Identifier: Apache-2.0

#include "data_watcher.hpp"

#include <phosphor-logging/lg2.hpp>

#include <string>
#include <unordered_set>

namespace data_sync
{
namespace watch::inotify
{

void DataWatcher::printWD(const std::map<int, fs::path>& _watchDescriptors)
{
    lg2::info("***Print current Watch descriptors***");
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

fs::path DataWatcher::getExistingParentPath(const fs::path& dataPath)
{
    fs::path parentPath = dataPath.parent_path();
    while (!fs::exists(parentPath))
    {
        parentPath = parentPath.parent_path();
    }
    return parentPath;
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
    lg2::info("Started monitoring {PATH}", "PATH", currentPathToWatch);
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
        lg2::info("Given path [{PATH}] doesn't exist to watch", "PATH",
                  currentPathToWatch);

        if ((eventMasksToWatch & IN_CREATE) == 0)
        {
            eventMasksToWatch |= IN_CREATE;
        }
        addToWatchList(getExistingParentPath(currentPathToWatch),
                       eventMasksToWatch);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<RequiredAction> DataWatcher::onDataChange()
{
    // NOLINTNEXTLINE
    co_await _fdioInstance->next();

    std::optional<std::vector<eventInfo>> receivedEvents = readEvents();

    if (receivedEvents.has_value())
    {
        std::unordered_set<RequiredAction> actionRequested{};
        std::ranges::for_each(
            receivedEvents.value(),
            [this, &actionRequested](const auto& receivedEventInfo) {
            auto requiredAction = processReceivedEvents(receivedEventInfo);
            if (requiredAction != RequiredAction::SKIP)
            {
                actionRequested.emplace(requiredAction);
            }
        });

        if (actionRequested.size() == 1)
        {
            co_return *std::begin(actionRequested);
        }
    }
    co_return RequiredAction::SYNC;
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

        lg2::debug("Received an inotify event for wd : {WD} with mask : {MASK}",
                   "MASK", receivedEvent->mask, "WD", receivedEvent->wd);
        offset += offsetof(inotify_event, name) + receivedEvent->len;
    }
    return receivedEvents;
}

RequiredAction DataWatcher::processReceivedEvents(eventInfo receivedEventInfo)
{
    if ((std::get<2>(receivedEventInfo) & IN_CLOSE_WRITE) != 0)
    {
        // Sync files upon modifications
        return processCloseWrite(receivedEventInfo);
    }
    else if (((std::get<2>(receivedEventInfo) & IN_CREATE) != 0) &&
             ((std::get<2>(receivedEventInfo) & IN_ISDIR) != 0))
    {
        /**
         * Handle the creation of directories inside a monitoring DIR
         */
        return processCreate(receivedEventInfo);
    }
    else if ((std::get<2>(receivedEventInfo) & IN_DELETE_SELF) != 0)
    {
        return processDeleteSelf(receivedEventInfo);
    }
    else if ((std::get<2>(receivedEventInfo) & IN_DELETE) != 0)
    {
        return processDelete(receivedEventInfo);
    }
    return RequiredAction::SKIP;
}

RequiredAction DataWatcher::processCloseWrite(eventInfo receivedEventInfo)
{
    // Case 1 : Files listed in the config and are already existing
    if (_watchDescriptors.at(std::get<0>(receivedEventInfo)) ==
        _dataPathToWatch)
    {
        lg2::info("Syncing {DATA} for IN_CLOSE_WRITE", "DATA",
                  _dataPathToWatch);
        return RequiredAction::SYNC;
    }
    // Case 2 : Files created while monitoring parent dir as file not
    // exists.
    else if (_watchDescriptors.at(std::get<0>(receivedEventInfo)) /
                 std::get<1>(receivedEventInfo) ==
             _dataPathToWatch)
    {
        addToWatchList(_dataPathToWatch, _eventMasksToWatch);
        removeWatch(std::get<0>(receivedEventInfo));
        return RequiredAction::SYNC;
    }

    return RequiredAction::SKIP;
}

RequiredAction DataWatcher::processCreate(eventInfo receivedEventInfo)
{
    // TODO : Add if check whether the IN_CREATE received for dir IN_ISDIR

    // Case 1 : When monitoring parent/grand parent directory as the
    // directory to be watched is not existing.
    fs::path createdPath =
        _watchDescriptors.at(std::get<0>(receivedEventInfo)) /
        std::get<1>(receivedEventInfo);
    if (_dataPathToWatch.string().starts_with(createdPath.string()))
    {
        if ((std::get<2>(receivedEventInfo) & IN_ISDIR) != 0)
        {
            auto modifyWatchIfExpected = [this](const fs::path& entry) {
                if (fs::is_directory(entry) &&
                    (_dataPathToWatch.string().starts_with(entry.string())))
                {
                    // Add watch for sub dirs and remove its parent watch
                    // until the JSON configured DIR creates.
                    addToWatchList(entry, _eventMasksToWatch);

                    auto isEntryParent = [&entry](const auto& wd) {
                        return (wd.second == entry.parent_path());
                    };
                    auto parentWd = std::ranges::find_if(_watchDescriptors,
                                                         isEntryParent);
                    if (parentWd != _watchDescriptors.end())
                    {
                        removeWatch(parentWd->first);
                    }
                    return true;
                }
                else if (fs::is_directory(entry) &&
                         (entry.string().starts_with(
                             _dataPathToWatch.string())))
                {
                    // Add watch for sub dirs and don't remove it's parent
                    // as created DIRs are childs of the configured DIR.
                    addToWatchList(entry, _eventMasksToWatch);
                    return true;
                }
                return false;
            };
            auto monitoringPath =
                _watchDescriptors.at(std::get<0>(receivedEventInfo));
            std::ranges::for_each(
                fs::recursive_directory_iterator(monitoringPath),
                modifyWatchIfExpected);
            return RequiredAction::SYNC;
        }
        return RequiredAction::SKIP;
    }
    // Case 2: While Monitoring the directory as per the JSON config.
    // Case 3: When sub directory got created inside watching directory
    else if (createdPath.string().starts_with(_dataPathToWatch.string()))
    {
        // TODO : Add if check whether the IN_CREATE received for dir
        // IN_ISDIR
        // ??

        // add watch for the created child subdirectories
        fs::path createdPath =
            _watchDescriptors.at(std::get<0>(receivedEventInfo)) /
            std::get<1>(receivedEventInfo);
        if (fs::is_directory(createdPath))
        {
            createWatchers(createdPath, _eventMasksToWatch);
        }
        return RequiredAction::SYNC;
    }
    return RequiredAction::SKIP;
}

RequiredAction DataWatcher::processDeleteSelf(eventInfo receivedEventInfo)
{
    // Case 1 : A monitoring file got deleted.
    // case 2 : A monitoring directory got deleted.
    lg2::debug("Received IN_DELETE_SELF from {PATH} , eventmask : {MASK}",
               "PATH", _watchDescriptors.at(std::get<0>(receivedEventInfo)),
               "MASK", std::get<2>(receivedEventInfo));

    fs::path deletedPath = _watchDescriptors.at(std::get<0>(receivedEventInfo));

    if (_watchDescriptors.size() == 1)
    {
        // Since no more watches will be there, add a watch on parent dir to
        // notify future create events.
        addToWatchList(getExistingParentPath(deletedPath), _eventMasksToWatch);
    }

    auto removeWatchIfChild = [this, &deletedPath](const auto& wd) {
        if (wd.second.string().starts_with(deletedPath.string()) &&
            (wd.second != deletedPath))
        {
            removeWatch(wd.first);
        }
    };
    std::ranges::for_each(_watchDescriptors, removeWatchIfChild);

    // Remove watch for deleted DIR also
    removeWatch(std::get<0>(receivedEventInfo));

    return RequiredAction::DELETE;
}

RequiredAction DataWatcher::processDelete(eventInfo receivedEventInfo)
{
    lg2::debug("Received IN_DELETE from {PATH} , eventmask : {MASK}", "PATH",
               _watchDescriptors.at(std::get<0>(receivedEventInfo)), "MASK",
               std::get<2>(receivedEventInfo));
    if ((std::get<2>(receivedEventInfo) & IN_ISDIR) == 0)
    {
        // Case 1 : A file inside watching directory got deleted
        return RequiredAction::DELETE;
    }

    return RequiredAction::SKIP;
}

void DataWatcher::removeWatch(int wd)
{
    fs::path dataPath = _watchDescriptors.at(wd);
    try
    {
        inotify_rm_watch(_inotifyFileDescriptor(), wd);
        _watchDescriptors.erase(wd);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to remove the watch for {PATH}", "PATH", dataPath);
    }

    lg2::info("Stopped monitoring {PATH}", "PATH", dataPath);
    printWD(_watchDescriptors);
}

} // namespace watch::inotify
} // namespace data_sync
