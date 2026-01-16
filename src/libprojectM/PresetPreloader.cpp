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

#include "PresetPreloader.hpp"

#include "Logging.hpp"
#include "Preset.hpp"
#include "PresetFactoryManager.hpp"

#include <algorithm>

namespace libprojectM {

PresetPreloader::PresetPreloader(PresetFactoryManager& factoryManager)
    : m_factoryManager(factoryManager)
    , m_workerThread(&PresetPreloader::WorkerThread, this)
{
}

PresetPreloader::~PresetPreloader()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopRequested = true;
    }
    m_condition.notify_one();

    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }
}

void PresetPreloader::PreloadPreset(const std::string& filename)
{
    if (filename.empty())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Check if already cached
        for (const auto& cached : m_cache)
        {
            if (cached.filename == filename && cached.ready)
            {
                return;
            }
        }

        m_pendingFilename = filename;
        m_hasPendingRequest = true;
    }
    m_condition.notify_one();
}

void PresetPreloader::CancelPendingPreload()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hasPendingRequest = false;
    m_pendingFilename.clear();
}

auto PresetPreloader::TryGetPreloadedPreset(const std::string& filename) -> std::unique_ptr<Preset>
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_cache.begin(), m_cache.end(),
                           [&filename](const PreloadedPreset& p) {
                               return p.filename == filename && p.ready && !p.failed;
                           });

    if (it != m_cache.end())
    {
        auto preset = std::move(it->preset);
        m_cache.erase(it);
        return preset;
    }

    return nullptr;
}

auto PresetPreloader::GetLastError() const -> std::string
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

void PresetPreloader::WorkerThread()
{
    while (true)
    {
        std::string filenameToLoad;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_condition.wait(lock, [this] {
                return m_stopRequested || m_hasPendingRequest;
            });

            if (m_stopRequested)
            {
                return;
            }

            if (!m_hasPendingRequest)
            {
                continue;
            }

            filenameToLoad = m_pendingFilename;
            m_hasPendingRequest = false;
            m_pendingFilename.clear();
        }

        // Perform CPU-intensive loading outside the lock
        PreloadedPreset preloaded;
        preloaded.filename = filenameToLoad;

        try
        {
            preloaded.preset = m_factoryManager.CreatePresetFromFile(filenameToLoad);
            preloaded.ready = true;
            preloaded.failed = false;

            LOG_DEBUG("Preloaded preset: " + filenameToLoad);
        }
        catch (const std::exception& ex)
        {
            preloaded.ready = true;
            preloaded.failed = true;
            preloaded.errorMessage = ex.what();

            LOG_WARN("Failed to preload preset: " + filenameToLoad + " - " + ex.what());
        }
        catch (...)
        {
            preloaded.ready = true;
            preloaded.failed = true;
            preloaded.errorMessage = "Unknown error";

            LOG_WARN("Failed to preload preset: " + filenameToLoad + " - Unknown error");
        }

        // Store in cache
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (preloaded.failed)
            {
                m_lastError = preloaded.errorMessage;
            }
            else
            {
                // Evict oldest if cache is full
                while (m_cache.size() >= maxCacheSize)
                {
                    m_cache.pop_back();
                }

                m_cache.push_front(std::move(preloaded));
            }
        }
    }
}

} // namespace libprojectM
