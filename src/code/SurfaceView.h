#ifndef SURFACEVIEW_H
#define SURFACEVIEW_H

#include "View.h"

class Surface;

class SurfaceView : public View
{
public:
    SurfaceView(Surface *surface);

    VIEW_RENDER_MODE renderMode() const override;
    void setPosC(const LPoint &pos) override;
    const LPoint *posC() const override;
    const LPoint *sizeC() const override;
    const LTexture *texture() const override;
    bool mapped() const override;
    Int32 scale() const override;
    UInt32 damageId() const override;
    const LRegion *inputRegionC() const override;
    const LRegion *opaqueRegionC() const override;
    const LRegion *translucentRegionC() const override;
    const LRegion *damageC() const override;

    // Events
    void paintBegin(Output *output) override;
    void enterOutput(Output *output) override;
    void leaveOutput(Output *output) override;
    void requestNextFrame(bool clearDamage = true) override;


    Surface *surface = nullptr;
};

#endif // SURFACEVIEW_H
