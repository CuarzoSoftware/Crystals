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
 * the same curve as its workspace sliding in or out (see Workspaces), so both get there together. Once the client draws its new size (fullscreen
 * or back to a window), that frame is snapshotted too and fades in over the first one. The window
 * itself is hidden meanwhile and shown when it ends.
 *
 * Snapshots are baked on the main thread (an auxiliary scene, like MinimizeAnimation's).
 */
class FullscreenAnimation final : public CZObject
{
public:
    static constexpr UInt32 FadeMs { 140 };      // The new frame fading in
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

    /// Whether frames of @p surface must keep being requested (it's drawn by an animation).
    static bool WantsFrames(const LSurface *surface) noexcept { return HidesSurface(surface); }

    static void Clear() noexcept;

    ~FullscreenAnimation();

private:
    FullscreenAnimation(ToplevelRole &toplevel, bool entering, LOutput *output) noexcept;
    void update() noexcept;
    void finish() noexcept;
    void bakeNew() noexcept;
    SkRect targetRect() const noexcept;
    /// The window as shown (decorations and their shadow included). @p margins: the shadow's room
    /// around the window (title bar + content) in the image.
    static std::shared_ptr<RImage> Bake(ToplevelRole &toplevel, Int32 scale, SkIRect &margins) noexcept;
    void place(AKImage &node, const SkRect &window, const SkIRect &margins) noexcept;

    CZWeak<ToplevelRole> m_toplevel;
    const LSurface *m_surface;
    bool m_entering;
    bool m_finished { false };
    bool m_applied { false };   // The fullscreen state was applied
    bool m_hasNew { false };    // The new frame was baked
    Int64 m_newMs { 0 };        // When (animation time)
    Int32 m_scale { 1 };
    SkRect m_from {};
    SkRect m_target {};         // Estimated until applied
    AKImage m_old;
    AKImage m_new;
    SkIRect m_oldMargins {}, m_newMargins {}; // Shadow room around the window in each image (LTRB)
    CZLinearAnimation m_anim;
};

#endif // FULLSCREENANIMATION_H
