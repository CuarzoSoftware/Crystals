#ifndef DNDRETURNANIMATION_H
#define DNDRETURNANIMATION_H

#include <Core/Types.h>
#include <AK/Nodes/AKImage.h>
#include <Core/CZLinearAnimation.h>
#include <Core/CZObject.h>
#include <memory>

/**
 * @brief A drag & drop icon sliding back to where the drag started, after a drop nobody accepted
 *        (or on nothing), like on macOS. Destroyed once it gets there.
 *
 * Shows a snapshot of the icon (its last buffer), since the client unmaps the real one right away.
 */
class DNDReturnAnimation final : public CZObject
{
public:
    /// Slides @p image from @p from (global rect) to @p to (its global top-left at the start).
    static void Start(std::shared_ptr<RImage> image, const SkRect &from, const SkPoint &to, UInt32 durationMs = 350) noexcept;

    /**
     * @brief Keeps @p image at @p rect for @p durationMs, then removes it.
     *
     * Bridges a drop to the client's own animation of the icon: there's no frame without it.
     */
    static void Hold(std::shared_ptr<RImage> image, const SkRect &rect, UInt32 durationMs) noexcept;

    /// Drops all animations (call before the scene is destroyed).
    static void Clear() noexcept;

    ~DNDReturnAnimation();

private:
    DNDReturnAnimation(std::shared_ptr<RImage> image, const SkRect &from, const SkPoint &to, UInt32 durationMs) noexcept;
    void update(Float64 t) noexcept;

    SkRect m_from;
    SkPoint m_to;
    AKImage m_node;
    CZLinearAnimation m_anim;
};

#endif // DNDRETURNANIMATION_H
