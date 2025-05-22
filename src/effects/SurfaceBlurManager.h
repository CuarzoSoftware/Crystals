#ifndef SURFACEBLURMANAGER_H
#define SURFACEBLURMANAGER_H

#include <core/Global.h>
#include <LBackgroundBlur.h>

class SurfaceBlurManager final : public LBackgroundBlur
{
public:
    SurfaceBlurManager(const void *params) noexcept;
    void configureRequest() override;
    void propsChanged(LBitset<PropChanges> changes, const Props &prevProps) override;
};

#endif // SURFACEBLURMANAGER_H
