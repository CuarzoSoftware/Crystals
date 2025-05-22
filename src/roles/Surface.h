#ifndef SURFACE_H
#define SURFACE_H

#include <core/Global.h>
#include <LSurface.h>
#include <ui/SurfaceView.h>

class Surface final : public LSurface
{
public:
    Surface(const void *params) noexcept : LSurface(params) {};

    void damageChanged() override;
    void opaqueRegionChanged() override;
    void invisibleRegionChanged() override;
    void inputRegionChanged() override;

    void bufferSizeChanged() override;
    void sizeChanged() override;
    void srcRectChanged() override;
    void bufferScaleChanged() override;
    void bufferTransformChanged() override;

    void roleChanged() override;
    void parentChanged() override;
    void mappingChanged() override;

    void orderChanged() override;
    void layerChanged() override;

    SurfaceView view { *this };
    bool needsTextureUpdate { true };
};

#endif // SURFACE_H
