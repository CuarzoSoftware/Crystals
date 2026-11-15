#ifndef FULLSCREENANIMATION_H
#define FULLSCREENANIMATION_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZWeak.h>
#include <Core/CZLinearAnimation.h>
#include <AK/Nodes/AKImage.h>
#include <memory>

/**
 * @brief A window entering or leaving fullscreen (like macOS).
 *
 * The window is snapshotted as it is, before it's configured. The snapshot then grows from the
 * window's rect to the whole output (or shrinks from the output to where the window lands), along
 * the same curve as its workspace sliding in or out (see Workspaces), so both get there together.
 *
 * Once the client draws its new size (fullscreen or back to a window), that frame is snapshotted
 * too and dissolves in over the first one, like macOS: both are stretched to the same rect, the new
 * one fades in over the first (which stays opaque, so the mix never lets the background through)
 * following the window's movement (so it eases out with it), done by FadeEndProgress of the way:
 *
 * - Entering fullscreen: the window frame stays opaque underneath the whole time, the fullscreen
 *   one fading in over it.
 * - Leaving it: the window frame fades in over the fullscreen one at a moderate pace
 *   (LeavingNewFadeSpeed), while the fullscreen frame fades out slowly, from LeavingOldFadeDelay of
 *   the dissolve to its end (the background hardly shows through).
 *
 * The frames follow the world when it moves otherwise (e.g. a swipe to another workspace during
 * the animation): see slideDeviation().
 *
 * The new frame stays live: it's snapshotted again whenever the window (or a subsurface) commits
 * new content, so e.g. a video keeps playing during the animation. The window itself is hidden
 * meanwhile (keeping receiving frame callbacks, see WantsFrames()) and shown when it ends.
 *
 * Snapshots are baked on the main thread (see Snapshot).
 */
class FullscreenAnimation final : public CZObject
{
public:
    static constexpr UInt32 FadeMs { 350 };            // The dissolve when the new frame came too late to follow the movement
    static constexpr Float64 FadeEndProgress { 0.99 }; // The new frame is fully in by then (of the movement)
    static constexpr Float64 MinFadeProgress { 0.2 };  // Less movement left than this when it came: FadeMs instead
    static constexpr SkScalar LeavingNewFadeSpeed { 1.25f }; // Leaving: the window frame fades in at this pace (of the dissolve)
    static constexpr SkScalar LeavingOldFadeDelay { 0.3f };  // Leaving: the fullscreen frame fades out slowly from there (of the dissolve)
    static constexpr UInt32 TimeoutMs { 1500 };  // The client never drew its new size: ends anyway

    /**
     * @brief Starts it for a window about to enter (on @p output) or leave fullscreen.
     *
     * Call before configuring it (the snapshot is the window as it is).
     */
    static void Begin(ToplevelRole &toplevel, bool entering, LOutput *output) noexcept;

    /// The window's fullscreen state was applied (it drew its new size): snapshots that frame.
    static void StateApplied(ToplevelRole &toplevel) noexcept;

    /// Whether @p surface is hidden by a running animation.
    static bool HidesSurface(const LSurface *surface) noexcept;

    /**
     * @brief Whether frames of @p surface must keep being requested: it's the window of a running
     *        animation or one of its subsurfaces.
     *
     * They're hidden (never rendered), but the client must keep drawing: its new size is awaited,
     * and many draw their content in subsurfaces, waiting for their frame callbacks.
     */
    static bool WantsFrames(const LSurface *surface) noexcept;

    /// Snapshots the new frames of windows whose state was applied, and again when they draw new
    /// content (Output::paintGL(), before rendering).
    static void ProcessPending() noexcept;

    static void Clear() noexcept;

    ~FullscreenAnimation();

private:
    FullscreenAnimation(ToplevelRole &toplevel, bool entering, LOutput *output) noexcept;
    void update() noexcept;
    void finish() noexcept;
    void bakeNew() noexcept;
    UInt64 treeDamage() const noexcept; // Changes whenever the window or a subsurface commits new content
    SkRect targetRect() const noexcept;
    SkScalar slideDeviation(SkScalar t) noexcept;
    void place(AKImage &node, const SkRect &window, const SkIRect &margins) noexcept;

    CZWeak<ToplevelRole> m_toplevel;
    const LSurface *m_surface;
    bool m_entering;
    bool m_finished { false };
    bool m_applied { false };   // The fullscreen state was applied
    bool m_hasNew { false };    // The new frame was baked
    Int64 m_newMs { 0 };        // When (animation time)
    Float64 m_newProgress { 0 };// The movement's progress then
    UInt64 m_newDamage { 0 };   // treeDamage() when the new frame was last snapshotted
    bool m_hasSlideStart { false };
    bool m_slideDisturbed { false }; // The world moved otherwise (see slideDeviation())
    SkScalar m_slideStart { 0.f }; // The destination workspace's slideX() at t = 0 (see slideDeviation())
    Int64 m_durationMs { 0 };   // The movement (to within ~0.3%)
    Int32 m_scale { 1 };
    SkRect m_from {};
    SkRect m_target {};         // Estimated until applied
    AKImage m_old;
    AKImage m_new;
    SkIRect m_oldMargins {}, m_newMargins {}; // Shadow room around the window in each image (LTRB)
    CZLinearAnimation m_anim;
};

#endif // FULLSCREENANIMATION_H
