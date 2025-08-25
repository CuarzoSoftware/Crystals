#ifndef SURFACE_H
#define SURFACE_H

#include <Core/Types.h>
#include <Core/Log.h>
#include <Scene/SurfaceView.h>
#include <LSurface.h>

class Surface final : public LSurface
{
public:
    Surface(const void *params) noexcept : LSurface(params)
    {
        log = Log.newWithContext("Surface");
        log(CZTrace, CZLN, "Surface created");
    };

    void damageChanged() override;
    void opaqueRegionChanged() override;
    void invisibleRegionChanged() override;
    void inputRegionChanged() override;

    void bufferSizeChanged() override;
    void sizeChanged() override;
    void srcRectChanged() override;
    void bufferScaleChanged() override;
    void bufferTransformChanged() override;

    void roleChanged(LBaseSurfaceRole *prevRole) override;
    void parentChanged() override;
    void mappingChanged() override;

    void orderChanged() override;
    void layerChanged() override;

    SurfaceView view { *this };
    CZLogger log;
};

#endif // SURFACE_H
