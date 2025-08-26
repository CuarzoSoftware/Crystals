#ifndef SURFACEVIEW_H
#define SURFACEVIEW_H

#include <Nodes/AKImage.h>
#include <Nodes/AKContainer.h>

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
    Surface &surface;

    // Subsurfaces below
    AKContainer below { YGFlexDirectionColumn, false, this };

    // This surface view
    AKImage view { this };

    // Subsurfaces above
    AKContainer above { YGFlexDirectionColumn, false, this };

    // Child popups
    AKContainer popups { YGFlexDirectionColumn, false, this };

    // Child toplevels
    AKContainer toplevels { YGFlexDirectionColumn, false, this };
};

#endif // SURFACEVIEW_H
