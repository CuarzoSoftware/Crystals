#ifndef SURFACEVIEW_H
#define SURFACEVIEW_H

#include <core/Global.h>
#include <AK/nodes/AKRenderableImage.h>
#include <AK/effects/AKBackgroundBlurEffect.h>

class Surface;

// TODO: Make this abstract and create one for each role

class SurfaceView final : public AKRenderableImage
{
public:
    SurfaceView(Surface &surface) noexcept;
    void syncPos() noexcept;
    void syncSize() noexcept;
    void syncDamage() noexcept;
    void syncOpaqueRegion() noexcept;
    void syncInvisibleRegion() noexcept;
    void syncInputRegion() noexcept;
    void syncTexture() noexcept;
    void syncScale() noexcept;
    void syncTransform() noexcept;
    void syncSrcRect() noexcept;
    void syncVisibility() noexcept;
    Surface &surface;
    AKBackgroundBlurEffect blur;
};

#endif // SURFACEVIEW_H
