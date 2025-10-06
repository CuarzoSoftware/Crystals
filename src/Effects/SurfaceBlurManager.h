#ifndef SURFACEBLURMANAGER_H
#define SURFACEBLURMANAGER_H

#include <Core/Types.h>
#include <Effects/AKBackgroundBlurEffect.h>
#include <LBackgroundBlur.h>

class SurfaceBlurManager final : public LBackgroundBlur
{
public:
    SurfaceBlurManager(const void *params) noexcept;
    void configureRequest() override;
    void propsChanged(CZBitset<PropChanges> changes, const Props &prevProps) override;

    AKBackgroundBlurEffect fx {};
};

#endif // SURFACEBLURMANAGER_H
