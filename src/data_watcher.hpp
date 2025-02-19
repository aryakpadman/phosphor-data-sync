// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/inotify.h>
#include <unistd.h>

#include <sdbusplus/async.hpp>

#include <filesystem>
#include <map>

namespace data_sync
{
namespace watch::inotify
{

namespace fs = std::filesystem;

/**
 * @brief A tuple which has the info related to the occured inotify event
 *
 * int         - Watch descriptor corresponds to the event
 * std::string - name[] in inotify_event struct
 * uint32_t    - Mask describing event
 */
using eventInfo = std::tuple<int, std::string, uint32_t>;

/**
 * @brief enum which indicates the type of action required against a
 * received inotify event
 */
enum class RequiredAction
{
    SYNC,
    DELETE,
    SKIP
};

/** @class FD
 *
 *  @brief RAII wrapper for file descriptor.
 */
class FD
{
  private:
    /** @brief File descriptor */
    int fd = -1;

  public:
    FD() = delete;
    FD(const FD&) = delete;
    FD& operator=(const FD&) = delete;
    FD(FD&&) = delete;
    FD& operator=(FD&&) = delete;

    /** @brief Saves File descriptor and uses it to do file operation
     *
     *  @param[in] fd - File descriptor
     */
    FD(int fd) : fd(fd) {}

    ~FD()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }

    int operator()() const
    {
        return fd;
    }
};

/** @class DataWatcher
 *
 *  @brief Adds inotify watch on directories/files configured for sync.
 *
 */
class DataWatcher
{
  public:
    DataWatcher(const DataWatcher&) = delete;
    DataWatcher& operator=(const DataWatcher&) = delete;
    DataWatcher(DataWatcher&&) = delete;
    DataWatcher& operator=(DataWatcher&&) = delete;

    /**
     * @brief Constructor
     *
     * Create watcher for directories/files to monitor for the occurence
     * of the interested events upon modifications.
     *
     *  @param[in] ctx - The async context object
     *  @param[in] inotifyFlags - inotify flags to watch
     *  @param[in] eventMasksToWatch - mask of interested events to watchch
     *  @param[in] dataPathToWatch - Path of the file/directory to be watched
     */
    DataWatcher(sdbusplus::async::context& ctx, int inotifyFlags,
                uint32_t eventMasksToWatch,
                const std::filesystem::path& dataPathToWatch);

    /**
     * @brief Destructor
     * Remove the inotify watch and close fd's
     */

    ~DataWatcher();

    /**
     * @brief API to monitor for the file/directory for inotify events
     */
    sdbusplus::async::task<RequiredAction> onDataChange();

    static void printWD(const std::map<int, fs::path>& _watchDescriptors);

  private:
    /**
     * @brief inotify flags
     */
    int _inotifyFlags;

    /**
     * @brief The group of interested event Masks for which data to be watched
     */
    uint32_t _eventMasksToWatch;

    /**
     * @brief File/Directory path to be watched
     */
    std::filesystem::path _dataPathToWatch;

    /** @brief The map of unique watch descriptors associated with an inotify
     * instance
     */
    std::map<int, fs::path> _watchDescriptors;

    /**
     * @brief file descriptor referring to the inotify instance
     */
    FD _inotifyFileDescriptor;

    /**
     * @brief fdio instance
     */
    std::unique_ptr<sdbusplus::async::fdio> _fdioInstance;

    /**
     * @brief initialize an inotify instance and returns file descriptor
     */
    int inotifyInit() const;

    /**
     * @brief API to get the existing parent path of a given path.
     *
     * @param[in] dataPath - The path to check

     * @returns std::filesystem::path - existing parent path
     */
    static fs::path getExistingParentPath(const fs::path& dataPath);

    /**
     * @brief Create watcher for the given data path and add to the list of
     * watch descriptors.
     *
     * @param[in] dataPathToWatch - The path of file/directory to be monitored
     * @param[in] eventMasksToWatch - The set of events for which the path to be
     *                                monitored
     */
    void addToWatchList(const fs::path& currentPathToWatch,
                        uint32_t eventMasksToWatch);

    /**
     * @brief API to check the given path andd watchers for the path and
     * subdirectories too if any.
     *
     * @param[in] currentPathToWatch - The path of file/directory to be
     * monitored
     * @param[in] eventMasksToWatch - The set of events for which the path to be
     *                                monitored
     */
    void createWatchers(const std::filesystem::path& currentPathToWatch,
                        uint32_t eventMasksToWatch);

    /**
     * @brief API to read the triggered events from inotify structure
     *
     * returns : std::optional<std::vector<eventInfo>>
     */
    std::optional<std::vector<eventInfo>> readEvents();

    /**
     * @brief API to trigger necessary actions based on the received inotify
     * events
     * @param[in] receivedEventInfo : Tuple of name and mask values of
     *                                inotify_event structure.
     * @returns bool : true - Required to the sync the data
     *                 false - Sync not required for the data
     */
    RequiredAction processReceivedEvents(eventInfo receivedEventInfo);

    /**
     * @brief API to handle the received IN_CLOSE_WRITE inotify events
     *
     * @param[in] receivedEventInfo : Tuple of name and mask values of
     *                                inotify_event structure.
     * @returns bool : true - Required to the sync the data
     *                 false - Sync not required for the data
     */
    RequiredAction processCloseWrite(eventInfo receivedEventInfo);

    /**
     * @brief API to handle the received IN_CREATE inotify events
     *
     * @param[in] receivedEventInfo : Tuple of name and mask values of
     *                                inotify_event structure.
     * @returns bool : true - Required to the sync the data
     *                 false - Sync not required for the data
     */
    RequiredAction processCreate(eventInfo receivedEventInfo);

    /**
     * @brief API to handle the received IN_DELETE inotify events
     *
     * @param[in] receivedEventInfo : Tuple of name and mask values of
     *                                inotify_event structure.
     * @returns bool : true - Required to the sync the data
     *                 false - Sync not required for the data
     */
    RequiredAction processDelete(eventInfo receivedEventInfo);

    /**
     * @brief API to handle the received IN_DELETE_SELF inotify events
     *
     * @param[in] receivedEventInfo : Tuple of name and mask values of
     *                                inotify_event structure.
     * @returns bool : true - Required to the sync the data
     *                 false - Sync not required for the data
     */
    RequiredAction processDeleteSelf(eventInfo receivedEventInfo);

    /** @brief API to remove the watch for a path and to
     *  remove from the map of watch descriptors.
     *
     *  @param[in] wd - Watch descriptor corresponding to the path
     */
    void removeWatch(int wd);
};

} // namespace watch::inotify
} // namespace data_sync
