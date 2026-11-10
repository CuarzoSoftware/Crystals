#ifndef SURFACEVIEW_H
#define SURFACEVIEW_H

#include <AK/Nodes/AKImage.h>
#include <AK/Nodes/AKContainer.h>

class Surface;

using namespace CZ;

class SurfaceView final : public AKContainer
{
public:
    SurfaceView(Surface &surface) noexcept;
    void syncPos() noexcept;
    void syncSize() noexcept;
    void syncDamage() noexcept;
    void syncOpaqueRegion() noexcept;
    void syncInvisibleRegion() noexcept;
    void syncInputRegion() noexcept;
    void syncImage() noexcept;
    void syncScale() noexcept;
    void syncTransform() noexcept;
    void syncSrcRect() noexcept;
    void syncVisibility() noexcept;

    /**
     * @brief Rounds the bottom corners of the content by @p radius (0 = square), e.g. server-side
     *        decorated toplevels (their top corners are under the title bar).
     *
     * The content is then drawn in four pieces: `view` (all but the bottom row), and the bottom row
     * split into the left corner, the middle and the right corner. The corners are masked by a
     * quarter disk; the rest is drawn as usual (and keeps its opaque region).
     */
    void setBottomRadius(Int32 radius) noexcept;
    Int32 bottomRadius() const noexcept { return m_bottomRadius; }

    Surface &surface;

    // Subsurfaces below
    AKContainer below { YGFlexDirectionColumn, false, this };

    // This surface view (when the bottom corners are rounded: all but the bottom row)
    AKImage view { this };

    // The bottom row when the bottom corners are rounded (see setBottomRadius())
    AKImage bottomLeft { this };
    AKImage bottomMiddle { this };
    AKImage bottomRight { this };

    // Subsurfaces above
    AKContainer above { YGFlexDirectionColumn, false, this };

    // Child popups
    AKContainer popups { YGFlexDirectionColumn, false, this };

    // Child toplevels
    AKContainer toplevels { YGFlexDirectionColumn, false, this };
private:
    bool split() const noexcept;
    void syncPieces() noexcept;
    SkRect pieceSrc(const SkIRect &piece) const noexcept;
    Int32 m_bottomRadius { 0 };
};

#endif // SURFACEVIEW_H
