#include <include/core/SkRegion.h>
#include <effects/SurfaceBlurManager.h>
#include <utils/Converter.h>
#include <roles/Surface.h>
#include <include/utils/SkParsePath.h>
#include <LClient.h>

SurfaceBlurManager::SurfaceBlurManager(const void *params) noexcept : LBackgroundBlur(params) {}

void SurfaceBlurManager::configureRequest()
{
    configureColorHint(ColorHint::Light);
    configureState(State::Enabled);
}

void SurfaceBlurManager::propsChanged(LBitset<PropChanges> changes, const Props &)
{
    auto &surface { *static_cast<Surface*>(this->surface()) };

    if (changes.check(PropChanges::ColorHintChanged))
    {
        // TODO: Implement dark blur in Kay
    }

    if (changes.check(PropChanges::RegionChanged))
    {
        SkRegion region;
        Converter::LtSRegion(this->region(), &region);
        surface.view.blur.setRegion(region);
    }

    if (changes.check(PropChanges::MaskChanged))
    {
        switch (maskType())
        {
        case MaskType::NoMask:
            surface.view.blur.clearClip();
            break;
        case MaskType::RoundRect:
        {
            AKRRect rRect;
            Converter::LtARRect(roundRectMask(), &rRect);
            surface.view.blur.setRoundRectClip(rRect);
            break;
        }
        case MaskType::SVGPath:
        {
            if (svgPathMask().empty())
                surface.view.blur.clearClip();
            else
            {
                SkPath path;
                if (SkParsePath::FromSVGString(svgPathMask().c_str(), &path))
                    surface.view.blur.setPathClip(path);
                else
                {
                    // Louvre does not validate if the SVG string is valid
                    surface.client()->destroyLater();
                    return;
                }
            }
        }
        }
    }

    if (changes.check(PropChanges::StateChanged))
    {
        if (state() == Enabled)
            surface.view.addBackgroundEffect(&surface.view.blur);
        else
            surface.view.removeBackgroundEffect(&surface.view.blur);
    }
}
