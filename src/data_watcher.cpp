// SPDX-License-Identifier: Apache-2.0

#include "data_watcher.hpp"

#include <phosphor-logging/lg2.hpp>

namespace data_sync
{
namespace watch::inotify
{

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
        _watchDescriptor = addToWatchList();
        lg2::info("Started watching the PATH : {PATH}", "PATH",
                  _dataPathToWatch);
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception : {EXCEPTION}", "EXCEPTION", e);
    }
}

DataWatcher::~DataWatcher()
{
    if ((_inotifyFileDescriptor() >= 0) && (_watchDescriptor >= 0))
    {
        inotify_rm_watch(_inotifyFileDescriptor(), _watchDescriptor);
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

int DataWatcher::addToWatchList()
{
    auto wd = inotify_add_watch(_inotifyFileDescriptor(),
                                _dataPathToWatch.c_str(), _eventMasksToWatch);

    if (-1 == wd)
    {
        lg2::error("inotify_add_watch call failed with ErrNo : {ERRNO}, "
                   "ErrMsg : {ERRMSG}",
                   "ERRNO", errno, "ERRMSG", strerror(errno));
        throw std::runtime_error("Failed to add to watch list");
    }
    else
    {
        lg2::debug("Watch added. PATH : {PATH} wd : {WD}", "PATH",
                   _dataPathToWatch, "WD", wd);
    }
    return wd;
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

        receivedEvents.emplace_back(receivedEvent->name, receivedEvent->mask);

        lg2::debug("Received inotify event with mask : {MASK}", "MASK",
                   receivedEvent->mask);
        offset += offsetof(inotify_event, name) + receivedEvent->len;
    }
    return receivedEvents;
}

bool DataWatcher::processReceivedEvents(eventInfo receivedEventInfo)
{
    if ((std::get<1>(receivedEventInfo) & IN_CLOSE_WRITE) != 0)
    {
        lg2::debug("Syncing {DATA}", "DATA", _dataPathToWatch);
        return true;
    }
    return false;
}

} // namespace watch::inotify
} // namespace data_sync
