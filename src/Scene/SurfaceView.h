#ifndef SURFACEVIEW_H
#define SURFACEVIEW_H

#include <Nodes/AKImage.h>

class Surface;

using namespace CZ;

class SurfaceView final : public AKImage
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
};

#endif // SURFACEVIEW_H
