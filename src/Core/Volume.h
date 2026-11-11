#ifndef VOLUME_H
#define VOLUME_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZTimer.h>
#include <Core/CZEventSource.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class VolumeHUD;

/**
 * @brief The keyboard's volume keys (up, down and mute), like on macOS.
 *
 * Each press changes the default output's volume by 1/16 (or toggles mute), shows the VolumeHUD
 * on the output with the cursor and plays a short feedback sound at the new level. Holding a key
 * repeats it. The keys are taken by the compositor (never sent to clients).
 *
 * Nothing blocks the compositor: wpctl, the sound player and reading the color scheme run on a
 * background thread, the HUD is updated when they're done (and right away with the expected level).
 * Volume changes from anywhere else don't show the HUD.
 */
class Volume final : public CZObject
{
public:
    /// Created on first use, destroyed by Clear() (before the scene).
    static Volume &Get() noexcept;
    static void Clear() noexcept;

    /// Handles a volume key event. Returns false if @p keyCode isn't a volume key.
    bool keyEvent(UInt32 keyCode, bool pressed) noexcept;

    ~Volume() noexcept;

private:
    Volume() noexcept;

    enum Action { Up, Down, Mute };
    void run(Action action) noexcept;

    // Background thread: jobs return what to do next on the main thread.
    using Job = std::function<std::function<void()>()>;
    void enqueue(Job job) noexcept;
    void loop() noexcept;

    std::unique_ptr<VolumeHUD> m_hud;

    // Last known state (-1: unknown yet).
    float m_level { -1.f };
    bool m_muted { false }, m_dark { false };

    // Key repeat (the compositor gets no repeats from the keyboard).
    UInt32 m_repeatKey { 0 };
    CZTimer m_repeat;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::vector<std::function<void()>> m_done;
    bool m_quit { false };
    std::shared_ptr<CZEventSource> m_wake;
    std::thread m_thread;
};

#endif // VOLUME_H
