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
     * @brief A surface's blurred background as last generated, to be shown under snapshots taken
     *        while its blur can't be regenerated (e.g. it's hidden by an animation, see
     *        FullscreenAnimation).
     */
    struct Blur
    {
        std::shared_ptr<CZ::RImage> image; ///< The blurred area
        SkIRect area {};                   ///< Where it is (surface coordinates)

        /**
         * true: generated for another size (see CaptureBlur()), stretched over the current
         * region's bounds. false: drawn at area as is (see Backdrop()).
         */
        bool stretch { true };
    };

    /**
     * @brief Precalculates the blurred background of a window where it is now, even if it's hidden
     *        or its workspace isn't shown: what's behind it there (the desktop picture and the
     *        windows below it in its workspace) blurred with its blur's radius and material.
     *
     * Covers the whole surface (area is its rect, stretch false). Empty if it has no blur.
     *
     * @param output The output its workspace belongs to (for the desktop picture).
     */
    Blur Backdrop(ToplevelRole &toplevel, LOutput *output, Int32 scale) noexcept;

    /**
     * @brief Captures the blurred background of @p surface as last shown (its region's bounds).
     *
     * Empty if it has no blur or it was never generated.
     */
    Blur CaptureBlur(Surface &surface, Int32 scale) noexcept;

    /**
     * @brief A window as shown: its surface and subsurfaces (clipped to the
     *        window: its content or geometry), decorations and their shadow (not
     *        its popups or child windows).
     *
     * @param margins Set to the shadow's room around the window (title bar + content) in the image.
     * @param blur    If set, drawn under it instead of its current blurred background (see View()).
     */
    std::shared_ptr<RImage> Toplevel(ToplevelRole &toplevel, Int32 scale, SkIRect &margins, const Blur *blur = nullptr) noexcept;

    /**
     * @brief The view of @p surface (with its mapped subsurfaces, even if hidden with it) at
     *        @p viewPos in an image of @p size.
     *
     * @param subsurfaceClip If set, the subsurfaces are clipped to it (surface coordinates).
     * @param blur If set, its image is drawn under the surface instead of the current blurred
     *             background (which may be stale, e.g. while the surface is hidden), stretched from
     *             its area to the bounds of the surface's current blur region and clipped to that
     *             region and the blur's current shape.
     */
    std::shared_ptr<RImage> View(Surface &surface, SkISize size, SkIPoint viewPos, Int32 scale,
                                 const SkIRect *subsurfaceClip = nullptr, const Blur *blur = nullptr) noexcept;

    /// Releases the auxiliary scene (before the main scene is destroyed).
    void Clear() noexcept;
}

#endif // SNAPSHOT_H
