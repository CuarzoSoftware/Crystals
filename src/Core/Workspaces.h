#ifndef WORKSPACES_H
#define WORKSPACES_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZWeak.h>
#include <Core/CZSpringAnimation.h>
#include <Core/Events/CZKeyboardKeyEvent.h>
#include <AK/Nodes/AKSolidColor.h>
#include <AK/Nodes/AKImage.h>
#include <chrono>
#include <memory>
#include <vector>

/**
 * @brief The workspaces of an output (like macOS Spaces).
 *
 * Desktop workspaces hold the windows that aren't fullscreen (each window belongs to one, see
 * ToplevelRole::desktopId: new ones, and those dragged here from another output, go to the current
 * desktop, or the first one while a fullscreen window's is shown), and show
 * the desktop (Desk: on the current desktop, or the last one shown; the others show a snapshot of
 * it while in view). The first desktop is always workspace 0 (it can't be moved or removed);
 * others are added (addDesktop()) and removed (their windows go to the first one). Each fullscreen
 * window gets its own workspace, created right after the current one when it goes fullscreen (and
 * removed when it leaves fullscreen, back to its desktop, or goes away). Workspaces are laid out left to
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

    /// Slightly underdamped: it settles much sooner than a critically damped spring (whose last few
    /// percent creep in), overshooting ~0.15%. Not for fullscreen transitions (see FullscreenDamping).
    static constexpr Float64 SpringDamping { 0.9 };

    /// When a window enters/leaves fullscreen (FullscreenAnimation follows it): critically damped, so
    /// it settles without the slightest bounce at the end (~98% at 0.44 s, settled by ~0.63 s).
    static constexpr Float64 FullscreenStiffness { 18000.0 };
    static constexpr Float64 FullscreenDamping { 1.0 };

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

    /// A window was activated: shows its workspace, keeping that window activated (not mapped yet:
    /// once mapped, see Surface::mappingChanged()).
    static void Reveal(ToplevelRole &toplevel) noexcept;

    /// Tells the dock again whether each output shows a fullscreen window's workspace (see ShellBus).
    static void NotifyFullscreenAll() noexcept;

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

    /// Every output shows its current workspace right away: slides and swipes in progress end
    /// (e.g. the session is being left, see SessionState).
    static void FinishAll() noexcept;

    /**
     * @brief The windows of an output going away, and the output they go to (see Evacuate()).
     */
    struct Evacuation
    {
        LOutput *target { nullptr }; ///< The closest output
        struct Window
        {
            CZWeak<ToplevelRole> toplevel;
            SkPoint rel;            ///< Where it was in the output (fraction of the room around it)
            SkIPoint pos;           ///< Its position then (its child windows move with it)
        };
        std::vector<Window> windows;
    };

    /**
     * @brief @p output is going away: finds its windows (all its workspaces', by where they are,
     *        so before Louvre removes it) and the closest output, and drops its workspaces.
     *
     * Then, once it's removed (and the outputs rearranged), Place() puts them on that output.
     */
    static Evacuation Evacuate(LOutput *output) noexcept;

    /**
     * @brief Puts evacuated windows on the current desktop of their output (see defaultDesktop()),
     *        floating (no longer fullscreen), where they were in the output, inside its available area
     *        (shrunk to fit if needed): all visible and draggable.
     */
    static void Place(const Evacuation &evacuation) noexcept;

    /// Where a window is relative to its output (see Placements()).
    struct Placement
    {
        CZWeak<ToplevelRole> toplevel;
        LOutput *output { nullptr };
        SkIPoint offset; ///< Its position minus the output's
    };

    /// Every window's output (its workspace's) and where it is on it: taken before the outputs are
    /// rearranged (e.g. by an output configuration client), see FollowOutputs().
    static std::vector<Placement> Placements() noexcept;

    /**
     * @brief Once the outputs were rearranged (moved, rotated, resized): each window moves with its
     *        output, and stays inside it (see ToplevelRole::keepReachable()).
     */
    static void FollowOutputs(const std::vector<Placement> &placements) noexcept;

    /**
     * @brief Makes every window reachable: inside the available area of its output (its workspace's,
     *        or the closest one if it's off every output). See ToplevelRole::keepReachable().
     *
     * Run again shortly after the outputs change (see ScheduleReachable()): the panels update the
     * available areas only once they've resized for the new outputs, and clients resize themselves.
     */
    static void EnsureReachable() noexcept;

    /// Runs EnsureReachable() soon (and once more later), after the outputs settle.
    static void ScheduleReachable() noexcept;

    /* -------------------------------- Per output -------------------------------- */

    /// An output's workspaces, to recreate them on another Output object (see SessionState).
    struct Saved
    {
        struct Space
        {
            UInt32 desktopId { 0 };        ///< A desktop's (0: a fullscreen window's)
            CZWeak<ToplevelRole> toplevel; ///< The fullscreen window
        };
        std::vector<Space> spaces; ///< In order (those being closed left out)
        size_t current { 0 };      ///< The one shown (index in spaces)
        UInt32 lastDesktopId { 0 };
    };

    Saved save() const noexcept;

    /**
     * @brief Recreates saved workspaces on these (new ones: just their first desktop): the desktops
     *        with the ids they had (their windows still refer to them), the fullscreen windows' ones
     *        if the window is still there and fullscreen (now on this output), showing the one that
     *        was shown (the first desktop if it's gone).
     */
    void restore(const Saved &saved) noexcept;

    Output &output() const noexcept { return m_output; }
    size_t count() const noexcept { return m_spaces.size(); }

    /// Whether workspace @p index was left by its window (removed once out of view).
    bool closing(size_t index) const noexcept { return index < m_spaces.size() && m_spaces[index]->closing; }

    /// The id of desktop @p index (0 if not a desktop, see ToplevelRole::desktopId).
    UInt32 desktopId(size_t index) const noexcept { return index < m_spaces.size() ? m_spaces[index]->desktopId : 0; }

    /// Whether workspace @p index is a desktop (not a fullscreen window's).
    bool isDesktop(size_t index) const noexcept { return index < m_spaces.size() && m_spaces[index]->desktopId != 0; }

    /// Where the desktop (Desk) is shown: the current workspace if a desktop, or the last desktop shown.
    size_t deskIndex() const noexcept;

    /// Adds a desktop workspace at the end. Returns its index.
    size_t addDesktop() noexcept;
    size_t current() const noexcept { return m_current; }

    /// The fullscreen window of workspace @p index (nullptr for the desktop).
    ToplevelRole *toplevel(size_t index) const noexcept;

    /// The fullscreen window shown (nullptr on the desktop).
    ToplevelRole *currentFullscreen() const noexcept { return toplevel(m_current); }

    /// Slides to workspace @p index (clamped), activating its window(s).
    void switchTo(Int64 index, Float64 velocity = 0.0, Float64 stiffness = SpringStiffness) noexcept;

    /// Like switchTo(), activating @p toplevel (in that workspace) instead of its topmost window.
    void switchTo(Int64 index, ToplevelRole *toplevel) noexcept;

    /**
     * @brief Like switchTo() without sliding (e.g. Exposé zooms into it instead).
     *
     * @param show Whether it's shown right away. Otherwise the workspace that was shown stays in
     *             view (its windows parked, but drawn where they were) until showCurrent().
     */
    void jumpTo(Int64 index, bool show = true) noexcept;

    /**
     * @brief Removes fullscreen workspace @p index (not the desktop): its window leaves fullscreen
     *        and goes back to the desktop (without FullscreenAnimation if not @p animated).
     */
    void destroy(size_t index, bool animated = true) noexcept;

    /// Moves fullscreen workspace @p from to position @p to (not the desktop's: it stays first).
    void move(size_t from, size_t to) noexcept;

    /// Shows the current workspace right away (see jumpTo()). Only visual (any thread, like the
    /// slide's steps).
    void showCurrent() noexcept;

    /// Whether it's sliding (or being swiped).
    bool moving() const noexcept;

    /// Whether it's being swiped (following the fingers).
    bool swiping() const noexcept { return m_swiping; }

    /// Where workspace @p index is drawn now, horizontally relative to the output (0 when shown).
    Int32 slideX(size_t index) const noexcept;

    /// The shown position (m_current once settled, fractional while sliding).
    Float64 pos() const noexcept { return m_pos; }

private:
    struct Space
    {
        CZWeak<ToplevelRole> toplevel; // A fullscreen window's (nullptr: a desktop, or closing)
        UInt32 desktopId { 0 };        // A desktop's (0: not a desktop)
        bool closing { false };        // Left by its window / removed: removed once out of view
        std::unique_ptr<AKNode> backdrop; // Black (fullscreen) or the desktop's snapshot (desktops)
        AKImage *deskImage { nullptr };   // The desktop's snapshot (desktops)
    };

    Space &insert(size_t index, ToplevelRole *toplevel) noexcept;

    /// Index of the desktop @p id (SIZE_MAX if none).
    size_t desktopIndex(UInt32 id) const noexcept;

    /// The desktop windows go to when they get here (new, dragged from another output...): the
    /// current one, or the first one if a fullscreen window's is shown (they aren't shown then).
    UInt32 defaultDesktop() const noexcept;

    /// Takes a snapshot of the desktop (Desk) for the desktops it isn't drawn on.
    void updateDeskImage() noexcept;

    UInt32 m_lastDesktopId { 0 }; // The last desktop shown
    void close(size_t index, Int64 switchTo, Float64 stiffness = SpringStiffness) noexcept;
    void removeClosed() noexcept;
    /// Activates the current workspace's window: @p preferred if given (e.g. the one being revealed,
    /// maybe not mapped yet), else its fullscreen window or the topmost one of the desktop.
    void activateCurrent(ToplevelRole *preferred = nullptr) noexcept;
    void startSpring(Float64 target, Float64 velocity, Float64 stiffness) noexcept;

    /// Tells the dock if the shown workspace became (or stopped being) a fullscreen window's (see ShellBus).
    void notifyFullscreen(bool force = false) noexcept;
    bool m_notifiedFullscreen { false };
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
