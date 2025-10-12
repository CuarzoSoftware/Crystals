#ifndef SURFACEBLURMANAGER_H
#define SURFACEBLURMANAGER_H

#include <AK/Effects/AKBackgroundBlurEffect.h>
#include <Louvre/Roles/LBackgroundBlur.h>
#include <Core/Types.h>

class SurfaceBlurManager final : public LBackgroundBlur
{
public:
    SurfaceBlurManager(const void *params) noexcept;
    void configureRequest() override;
    void propsChanged(CZBitset<PropChanges> changes, const Props &prevProps) override;

    AKBackgroundBlurEffect fx {};
};

#endif // SURFACEBLURMANAGER_H
