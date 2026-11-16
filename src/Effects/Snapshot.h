#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <Core/Types.h>
#include <CZ/skia/core/SkRect.h>
#include <memory>

namespace CZ { class RImage; }

/**
 * @brief Images of surfaces as shown, for animations and previews (main thread).
 *
 * The surface's view is moved into an auxiliary scene, rendered into an image and put back. Its
 * background blur (see SurfaceBlurManager) isn't rendered there (it'd blur nothing, and overwrite
 * what it blurred on screen): the image is drawn over that blurred background, as last shown.
 */
namespace Snapshot
{
    /**
     * @brief A window as shown: its surface and subsurfaces (clipped to the
     *        window: its content or geometry), decorations and their shadow (not
     *        its popups or child windows).
     *
     * @param margins Set to the shadow's room around the window (title bar + content) in the image.
     */
    std::shared_ptr<RImage> Toplevel(ToplevelRole &toplevel, Int32 scale, SkIRect &margins) noexcept;

    /**
     * @brief The view of @p surface (with its mapped subsurfaces, even if hidden with it) at
     *        @p viewPos in an image of @p size.
     *
     * @param subsurfaceClip If set, the subsurfaces are clipped to it (surface coordinates).
     */
    std::shared_ptr<RImage> View(Surface &surface, SkISize size, SkIPoint viewPos, Int32 scale,
                                 const SkIRect *subsurfaceClip = nullptr) noexcept;

    /// Releases the auxiliary scene (before the main scene is destroyed).
    void Clear() noexcept;
}

#endif // SNAPSHOT_H
