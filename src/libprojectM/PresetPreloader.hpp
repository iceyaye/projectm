/**
 * projectM -- Milkdrop-esque visualisation SDK
 * Copyright (C)2003-2024 projectM Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * See 'LICENSE.txt' included within this release
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace libprojectM {

class Preset;
class PresetFactoryManager;

/**
 * @brief Asynchronously preloads presets in a background thread.
 *
 * This class manages background loading of presets to reduce frame stutters
 * during preset transitions. The CPU-intensive work (file I/O, parsing,
 * expression compilation) is performed on a worker thread, while GPU
 * initialization must still happen on the main render thread.
 *
 * Usage:
 * 1. Call PreloadPreset() with the filename of the next preset
 * 2. Later, call TryGetPreloadedPreset() to retrieve it (if ready)
 * 3. If not ready, fall back to synchronous loading
 */
class PresetPreloader
{
public:
    /**
     * @brief Constructs a PresetPreloader with a reference to the factory manager.
     * @param factoryManager The factory manager used to create presets.
     */
    explicit PresetPreloader(PresetFactoryManager& factoryManager);

    /**
     * @brief Destructor. Signals the worker thread to stop and joins it.
     */
    ~PresetPreloader();

    // Non-copyable, non-movable (owns thread)
    PresetPreloader(const PresetPreloader&) = delete;
    PresetPreloader& operator=(const PresetPreloader&) = delete;
    PresetPreloader(PresetPreloader&&) = delete;
    PresetPreloader& operator=(PresetPreloader&&) = delete;

    /**
     * @brief Requests preloading of a preset in the background.
     *
     * If another preset is currently being loaded, this cancels that request
     * and starts loading the new one instead.
     *
     * @param filename The preset filename to preload.
     */
    void PreloadPreset(const std::string& filename);

    /**
     * @brief Cancels any pending preload request.
     *
     * Does not affect presets that have already finished loading.
     */
    void CancelPendingPreload();

    /**
     * @brief Attempts to retrieve a preloaded preset.
     *
     * If the preset has been preloaded and is ready, it is removed from the
     * cache and returned. The caller takes ownership of the preset.
     *
     * @param filename The filename of the preset to retrieve.
     * @return The preloaded preset, or nullptr if not available.
     */
    std::unique_ptr<Preset> TryGetPreloadedPreset(const std::string& filename);

    /**
     * @brief Returns the error message from the last failed preload.
     * @return The error message, or empty string if no error.
     */
    std::string GetLastError() const;

private:
    /**
     * @brief Internal structure to hold a preloaded preset and its state.
     */
    struct PreloadedPreset
    {
        std::unique_ptr<Preset> preset;
        std::string filename;
        std::string errorMessage;
        bool ready{false};
        bool failed{false};
    };

    /**
     * @brief Worker thread function that processes preload requests.
     */
    void WorkerThread();

    PresetFactoryManager& m_factoryManager;

    std::thread m_workerThread;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stopRequested{false};

    std::string m_pendingFilename;
    bool m_hasPendingRequest{false};

    std::list<PreloadedPreset> m_cache;
    std::string m_lastError;

    static constexpr size_t maxCacheSize = 2;
};

} // namespace libprojectM
