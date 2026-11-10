#ifndef MINIMIZEANIMATION_H
#define MINIMIZEANIMATION_H

#include <Core/Types.h>
#include <AK/Nodes/AKImage.h>
#include <Core/CZLinearAnimation.h>
#include <Core/CZWeak.h>
#include <Core/CZObject.h>

class Output;

/**
 * @brief Minimize / unminimize animation of a toplevel.
 *
 * The toplevel (with its subsurfaces, clipped to the toplevel surface) is baked into an image using
 * an auxiliary AKScene. That image is then shown by an AKImage node that moves and shrinks from the
 * window to the spot where it's docked (or grows back from it when unminimizing) and fades out (in).
 *
 * The spot is the rect set by a taskbar/dock through wlr-foreign-toplevel-management
 * (set_rectangle), read every frame so the dock may still be moving/growing its item. Without one,
 * the window shrinks towards the bottom center of its output.
 */
class MinimizeAnimation final : public CZObject
{
public:
    /// Starts the animation for a toplevel that was just (un)minimized, replacing any running one.
    static void Start(LToplevelRole &toplevel, bool minimizing) noexcept;

    /// Bakes pending snapshots and drops finished animations. Called from Output::paintGL() (the
    /// rendering thread) before the scene is rendered.
    static void ProcessPending(Output &output) noexcept;

    /// Whether the surface is being unminimized (its view stays hidden until the animation ends).
    static bool HidesSurface(const LSurface *surface) noexcept;

    /// Drops all animations and the auxiliary scene (call before the scene is destroyed).
    static void Clear() noexcept;

    ~MinimizeAnimation();

private:
    MinimizeAnimation(LToplevelRole &toplevel, bool minimizing) noexcept;
    bool bake(Output &output) noexcept;
    void update(Float64 t) noexcept;
    SkRect windowRect() const noexcept;
    SkRect dockRect() const noexcept;

    CZWeak<LToplevelRole> m_toplevel;
    LSurface *m_surface;
    bool m_minimizing;
    bool m_baked { false };
    bool m_finished { false };
    SkRect m_window {};     // Captured when it starts
    SkRect m_lastDock {};   // Last known dock rect (kept if the dock goes away)
    AKImage m_node;
    CZLinearAnimation m_anim;
};

#endif // MINIMIZEANIMATION_H
