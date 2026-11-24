#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <Core/Types.h>
#include <Core/Events/CZKeyboardKeyEvent.h>

class Output;

/**
 * @brief Screenshots saved to the desktop folder (XDG_DESKTOP_DIR, ~/Desktop by default).
 *
 * Print Screen or Super+Shift+3: every output is captured on its next frame, as shown (without the
 * cursor), each to "Screenshot <date> at <time>.png" (" (2)", " (3)", ... for the other outputs,
 * or if the name is taken). PNGs are encoded and written in a thread.
 */
namespace Screenshot
{
    /// Takes the shortcut (press and release, never sent to clients). Main thread.
    bool KeyEvent(const CZ::CZKeyboardKeyEvent &e) noexcept;

    /// Captures every output on its next frame. Main thread.
    void Take() noexcept;

    /// Called by Output::paintGL() once the frame is rendered: saves it if requested.
    void Capture(Output &output) noexcept;
}

#endif // SCREENSHOT_H
