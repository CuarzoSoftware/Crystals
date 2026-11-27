#ifndef SHOWDESKTOP_H
#define SHOWDESKTOP_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZWeak.h>
#include <Core/CZSpringAnimation.h>
#include <Core/Events/CZKeyboardKeyEvent.h>
#include <Core/Events/CZPointerButtonEvent.h>
#include <AK/Nodes/AKContainer.h>
#include <AK/Nodes/AKImage.h>
#include <chrono>
#include <list>
#include <memory>
#include <vector>

/**
 * @brief Show Desktop (like macOS's): the windows move aside, mostly off the screen and faded, so
 *        the desktop can be used. Only a thin strip of each one stays on screen.
 *
 * Every output showing a desktop workspace (see Workspaces) does it at once: the windows of its
 * current desktop (not minimized ones, nor fullscreen workspaces). Each window goes to the closest
 * side of its output (left or right) and the windows of each side are spread along it, top to
 * bottom in the order they were.
 *
 * The windows are shown through snapshots (see Snapshot), taken again whenever they draw (so they
 * stay live, like Exposé's), drawn clipped to their output (never on the others) and sliding with
 * their workspace. The windows themselves are hidden and moved far away meanwhile (see
 * ParkOffset(), like Workspaces' parked ones): input never reaches them, the desktop gets it. Only
 * the strips are hit-tested (see windowAt()).
 *
 * - Shown by a three finger swipe down (following the fingers) or Super+D.
 * - Left (the windows come back) by a swipe up, Super+D, or:
 *   - Clicking one of the strips: that window is activated (the strips take no other input).
 *   - Dragging something (drag & drop) over a strip: the windows come back to take the drop.
 *   - Switching workspaces (they come back while sliding away with theirs), opening Exposé (right
 *     away: it lays out the windows from where they are).
 *   - A window being activated (e.g. from the dock), mapped or restored.
 *   - The outputs changing (right away, before the windows are rearranged).
 *
 * Main thread, except the drawing (animation steps, ProcessPending()), as other effects.
 */
class ShowDesktop final : public CZObject
{
public:
    /// How much of each window stays on screen (logical pixels).
    static constexpr Int32 Peek { 12 };

    /// The windows' opacity once aside.
    static constexpr SkScalar Opacity { 0.25f };

    /// Swipe distance (touchpad units) to show/leave it all the way.
    static constexpr Float64 SwipeDistance { 320.0 };

    /// Springs (critically damped): showing/leaving, and following the fingers.
    static constexpr Float64 SpringStiffness { 13000.0 };
    static constexpr Float64 SwipeFollowStiffness { 70000.0 };

    /// Whether the windows are moved aside (or moving).
    static bool Active() noexcept;

    /// Whether it's shown or being shown (not leaving).
    static bool Shown() noexcept;

    /// Whether @p surface is hidden (its window shown through its snapshot).
    static bool HidesSurface(const LSurface *surface) noexcept;

    /// Whether frames of @p surface must keep being requested (its snapshot is redrawn).
    static bool WantsFrames(const LSurface *surface) noexcept { return HidesSurface(surface); }

    /// Added to the role position of hidden windows: far away, out of input's reach.
    static SkIPoint ParkOffset(const LSurface *surface) noexcept;

    /// Shows it if not shown, leaves it otherwise.
    static void Toggle() noexcept;

    /// The windows come back (animated).
    static void Leave() noexcept;

    /// The windows come back right away.
    static void Reset() noexcept;

    /// Super+D. Returns `true` if the key was taken.
    static bool KeyEvent(const CZKeyboardKeyEvent &e) noexcept;

    /// A vertical three finger swipe (see Pointer). Down shows it, up leaves it.
    static void SwipeBegin() noexcept;
    static void SwipeUpdate(const SkPoint &delta) noexcept;
    static void SwipeEnd(bool cancelled) noexcept;

    /// The pointer moved (already at the new position). Returns `true` if over a strip: no client
    /// gets it.
    static bool PointerMove() noexcept;

    /// A button. Returns `true` if taken (a strip clicked: that window comes back).
    static bool PointerButton(const CZPointerButtonEvent &e) noexcept;

    /// Something is being dragged (drag & drop) with the pointer: over a strip, the windows come back.
    static void DragMoved() noexcept;

    /// A window was activated, mapped or restored: the windows come back.
    static void WindowActivated(ToplevelRole *toplevel) noexcept;

    /// A surface drew: its window's snapshot is taken again on its output's next frame.
    static void SurfaceDamaged(const LSurface *surface) noexcept;

    /// Takes the snapshots of the windows on @p output that drew and places them (Output::paintGL(),
    /// before rendering).
    static void ProcessPending(Output &output) noexcept;

    static void Clear() noexcept;

    ~ShowDesktop();

private:
    enum class Side { Left, Right };

    struct Window
    {
        CZWeak<ToplevelRole> toplevel;
        Output *output { nullptr };
        size_t workspace { 0 };   // Slides with it (see Workspaces::slideX())
        Window *root { nullptr }; // Its root window (itself, or the parent of a dialog)
        Side side { Side::Left }; // Root windows: where it goes
        size_t slot { 0 }, slots { 1 }; // Root windows: its place along the side, among how many
        bool dirty { false };     // Drew since its last snapshot
        SkIRect margins {};       // Shadow room in the image
        AKImage node;
    };

    // The windows of an output, clipped to it: the inner container is offset back, so they're
    // placed in global coordinates (see Expose)
    struct Screen
    {
        Output *output { nullptr };
        AKContainer clip { YGFlexDirectionColumn, true };
        AKContainer layer { YGFlexDirectionColumn, false, &clip };
    };

    ShowDesktop() noexcept;
    static ShowDesktop *Get(bool create) noexcept;

    /// Collects the windows to move aside (returns `false` if there are none).
    bool activate() noexcept;
    void deactivate() noexcept;
    void animateTo(Float64 target, Float64 velocity) noexcept;

    /// Places the snapshots (where the windows are, aside by m_progress).
    void apply() noexcept;
    void snapshot(Window &w) noexcept;

    /// Where root window @p w goes, relative to where it is.
    SkIPoint fullOffset(const Window &w) const noexcept;

    /// Its window's frame (title bar and content, or geometry) as drawn now (global).
    SkRect shownFrame(const Window &w) const noexcept;

    /// The window whose strip is at @p pos (and not covered by a panel), or nullptr.
    ToplevelRole *windowAt(const SkPoint &pos) const noexcept;
    const Window *find(const LSurface *surface) const noexcept;

    // Bottom to top. Kept until the next time (deactivated maybe in an animation step)
    std::list<Window> m_windows;
    std::vector<std::unique_ptr<Screen>> m_screens;
    bool m_active { false };
    bool m_open { false };          // Where it's going
    Float64 m_progress { 0.0 };     // 0: windows where they are, 1: aside
    bool m_pressed { false };       // A strip pressed: its release is taken too

    CZSpringAnimation m_spring;

    bool m_swiping { false };
    Float64 m_swipeRaw { 0.0 };
    Float64 m_swipeVelocity { 0.0 };
    std::chrono::steady_clock::time_point m_swipeLast;
};

#endif // SHOWDESKTOP_H
