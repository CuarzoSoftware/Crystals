#ifndef WORKSPACES_H
#define WORKSPACES_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZWeak.h>
#include <Core/CZSpringAnimation.h>
#include <Core/Events/CZKeyboardKeyEvent.h>
#include <AK/Nodes/AKSolidColor.h>
#include <chrono>
#include <memory>
#include <vector>

/**
 * @brief The workspaces of an output (like macOS Spaces).
 *
 * Workspace 0 is the desktop: the desktop (Desk) and every window that isn't fullscreen. Each
 * fullscreen window gets its own workspace, created right after the current one when it goes
 * fullscreen (and removed when it leaves fullscreen or goes away). Workspaces are laid out left to
 * right and the output shows one at a time, sliding between them:
 * - Ctrl+Super+Left/Right.
 * - A horizontal three finger swipe (following the fingers, settling with a spring).
 * - Activating a window of another workspace (e.g. from the dock) switches to it.
 *
 * The docks and other layer surfaces stay in place. Top bars (e.g. Firmament's) are shown in the
 * workspace the user is in (so are their menus): auto-hidden in fullscreen ones (see
 * TopbarAutoHide, which also draws them in the desktop while sliding away from a fullscreen one).
 * Fullscreen workspaces have a black backdrop.
 *
 * Windows of workspaces not shown are moved far away (ParkOffset(), applied through their role
 * position so input never reaches them) and not drawn. While sliding, views are drawn at their
 * workspace's place (ViewOffset()); content of an output switching workspaces is only drawn on it.
 *
 * All of it runs on the main thread, except the drawing helpers called from Output::paintGL().
 */
class Workspaces final : public CZObject
{
public:
    /// Spring of the slide between workspaces (critically damped, ~0.5 s).
    static constexpr Float64 SpringStiffness { 15000.0 };

    /// Slightly faster when a window enters/leaves fullscreen (FullscreenAnimation follows it).
    static constexpr Float64 FullscreenStiffness { 20000.0 };

    /// Stiffer spring following the fingers while swiping (smooths fast movements, ~0.1 s lag).
    static constexpr Float64 SwipeFollowStiffness { 90000.0 };

    /// Space between workspaces while sliding (the background shows there), like macOS.
    static constexpr Int32 Gap { 72 };

    /// Swipe distance (touchpad units) to move one workspace.
    static constexpr Float64 SwipeDistance { 450.0 };

    /// Scales the finger movement (and so the release velocity), and the release velocity limit
    /// (workspaces per second).
    static constexpr Float64 SwipeSpeed { 1.0 };
    static constexpr Float64 SwipeMaxVelocity { 4.5 };

    /// Swiping past the last reachable workspace: at most this far (in workspaces), with this
    /// initial resistance (fraction of the finger movement followed), increasingly harder.
    static constexpr Float64 SwipeMaxOvershoot { 0.05 };
    static constexpr Float64 SwipeOvershootSlope { 0.2 };

    explicit Workspaces(Output &output) noexcept;
    ~Workspaces();

    /// The workspaces of @p output (created on first use).
    static Workspaces *Of(LOutput *output) noexcept;

    /* ------------------------------- Membership ------------------------------- */

    /// The workspace showing @p surface (its root window or the desktop), if any.
    static Workspaces *Find(const LSurface *surface, size_t *index = nullptr) noexcept;

    /// Whether @p surface belongs to a workspace not shown now (moved away, see ParkOffset()).
    static bool IsParked(const LSurface *surface) noexcept;

    /// Added to the role position of the windows (and desktop) of workspaces not shown.
    static SkIPoint ParkOffset(const LSurface *surface) noexcept;

    /// Added to the role position when drawing @p surface: its workspace's slide minus the park offset.
    static SkIPoint ViewOffset(const LSurface *surface) noexcept;

    /// Whether @p surface must not be drawn now (its workspace is out of view).
    static bool HidesSurface(const LSurface *surface) noexcept;

    /* --------------------------------- Events --------------------------------- */

    /// A toplevel was (un)fullscreened, (un)mapped, (un)minimized: creates/removes its workspace.
    static void Sync(ToplevelRole &toplevel) noexcept;

    /// A window is being destroyed: its workspace goes away.
    static void Removed(ToplevelRole &toplevel) noexcept;

    /// A window was activated: shows its workspace.
    static void Reveal(ToplevelRole &toplevel) noexcept;

    /// Ctrl+Super+Left/Right. Returns `true` if the key was taken.
    static bool KeyEvent(const CZKeyboardKeyEvent &e) noexcept;

    /// Three finger swipes. Return `true` if taken.
    static bool SwipeBegin(UInt32 fingers) noexcept;
    static bool SwipeUpdate(UInt32 fingers, const SkPoint &delta) noexcept;
    static bool SwipeEnd(bool cancelled) noexcept;

    /// Called by Output::paintGL() around rendering (content of switching outputs only drawn on them).
    static void BeginRender(LOutput *output) noexcept;
    static void EndRender() noexcept;

    static void Clear() noexcept;

    /* -------------------------------- Per output -------------------------------- */

    size_t count() const noexcept { return m_spaces.size(); }
    size_t current() const noexcept { return m_current; }

    /// The fullscreen window of workspace @p index (nullptr for the desktop).
    ToplevelRole *toplevel(size_t index) const noexcept;

    /// The fullscreen window shown (nullptr on the desktop).
    ToplevelRole *currentFullscreen() const noexcept { return toplevel(m_current); }

    /// Slides to workspace @p index (clamped), activating its window(s).
    void switchTo(Int64 index, Float64 velocity = 0.0, Float64 stiffness = SpringStiffness) noexcept;

    /// Whether it's sliding (or being swiped).
    bool moving() const noexcept;

    /// Where workspace @p index is drawn now, horizontally relative to the output (0 when shown).
    Int32 slideX(size_t index) const noexcept;

    /// The shown position (m_current once settled, fractional while sliding).
    Float64 pos() const noexcept { return m_pos; }

private:
    struct Space
    {
        CZWeak<ToplevelRole> toplevel; // nullptr: the desktop, or a closing workspace
        bool closing { false };        // Left by its window: removed once out of view
        std::unique_ptr<AKSolidColor> backdrop;
    };

    Space &insert(size_t index, ToplevelRole *toplevel) noexcept;
    void close(size_t index, Int64 switchTo, Float64 stiffness = SpringStiffness) noexcept;
    void removeClosed() noexcept;
    void activateCurrent() noexcept;
    void startSpring(Float64 target, Float64 velocity, Float64 stiffness) noexcept;
    void onPosChanged() noexcept;

    Output &m_output;
    std::vector<std::unique_ptr<Space>> m_spaces;
    size_t m_current { 0 };
    Float64 m_pos { 0.0 }; // Shown position: m_current once settled

    // The slide (in workspaces)
    CZSpringAnimation m_spring;

    // Swipe
    bool m_swiping { false };
    Float64 m_swipeVelocity { 0.0 }; // Workspaces per second
    Float64 m_swipeRaw { 0.0 };      // Where the fingers would put it (without the resistance)
    std::chrono::steady_clock::time_point m_swipeLast;
};

#endif // WORKSPACES_H
