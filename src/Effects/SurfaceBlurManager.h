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

    /**
     * @brief Applies the client's clip (mask) to the blur, intersected with the rounded bottom
     *        corners of the surface view, if any (server-side decorated windows, see
     *        SurfaceView::setBottomRadius()).
     *
     * Called when the mask, the radius or the surface size changes.
     */
    void updateClip() noexcept;

    /// Re-blurred while rendering (see Output::paintGL()): captures of its toplevel are redrawn.
    bool blurred { false };
};

#endif // SURFACEBLURMANAGER_H
