#include <CZ/skia/core/SkRegion.h>
#include <Effects/SurfaceBlurManager.h>
#include <Roles/Surface.h>
#include <LClient.h>

SurfaceBlurManager::SurfaceBlurManager(const void *params) noexcept : LBackgroundBlur(params) {}

void SurfaceBlurManager::configureRequest()
{
    configureColorHint(ColorHint::Light);
    configureState(State::Enabled);
}

void SurfaceBlurManager::propsChanged(CZBitset<PropChanges> changes, const Props &)
{
    auto &surface { *static_cast<Surface*>(this->surface()) };

    if (changes.has(PropChanges::ColorHintChanged))
    {
        // TODO: Implement dark blur in Kay
    }

    if (changes.has(PropChanges::RegionChanged))
    {
        fx.setRegion(props().region);
    }

    if (changes.has(PropChanges::MaskChanged))
    {
        switch (props().maskType)
        {
        case MaskType::NoMask:
            fx.clearClip();
            break;
        case MaskType::RoundRect:
        {
            fx.setRoundRectClip(props().roundRectMask);
            break;
        }
        case MaskType::SVGPath:
        {
            if (props().svgPathMask.isEmpty())
                fx.clearClip();
            else
                fx.setPathClip(props().svgPathMask);
        }
        }
    }

    if (changes.has(PropChanges::StateChanged))
    {
        if (props().state == Enabled)
            surface.view.view.addBackgroundEffect(&fx);
        else
            surface.view.view.removeBackgroundEffect(&fx);
    }
}
