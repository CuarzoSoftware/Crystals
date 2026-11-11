#include <Effects/VolumeHUD.h>
#include <Seat/Output.h>
#include <Scene/Scene.h>
#include <Louvre/Louvre.h>
#include <Louvre/Cursor/LCursor.h>
#include <cmath>

// Metrics (logical px), after macOS's volume overlay.
static constexpr Int32 Size { 200 };
static constexpr Int32 Radius { 18 };
static constexpr SkScalar BarWidth { 160.f };
static constexpr SkScalar BarHeight { 8.f };
static constexpr SkScalar SegmentGap { 2.f };

// Timing.
static constexpr UInt32 VisibleMs { 1100 };  // After the last change

VolumeHUD::VolumeHUD() noexcept :
    AKContainer(YGFlexDirectionColumn, false),
    m_blur(this)
{
    // Only shown: never takes input.
    const SkRegion empty;
    setInputRegion(&empty);
    m_icon.setInputRegion(&empty);
    m_bar.setInputRegion(&empty);

    layout().setPositionType(YGPositionTypeAbsolute);
    layout().setWidth(Size);
    layout().setHeight(Size);
    layout().setAlignItems(YGAlignCenter);
    layout().setJustifyContent(YGJustifyCenter);
    layout().setPadding(YGEdgeTop, 12.f);
    layout().setGap(YGGutterRow, 18.f);

    m_blur.setRoundRectClip(CZRRect(SkIRect::MakeWH(Size, Size), Radius, Radius, Radius, Radius));

    m_icon.enableReplaceImageColor(true);
    m_icon.layout().setWidth(104.f);
    m_icon.layout().setHeight(104.f);

    m_bar.layout().setWidth(BarWidth);
    m_bar.layout().setHeight(BarHeight);
    m_bar.layout().setGap(YGGutterColumn, SegmentGap);

    for (auto &segment : m_segments)
    {
        segment = std::make_unique<AKSolidColor>(CZAdaptiveColor(0), &m_bar);
        segment->setInputRegion(&empty);
        segment->layout().setFlex(1.f);
        segment->layout().setHeight(BarHeight);
    }

    m_hideTimer.setCallback([this](CZTimer*){ hide(); });
}

void VolumeHUD::show(float level, bool muted, bool dark) noexcept
{
    auto *output { static_cast<Output*>(CZ::cursor()->output()) };

    if (!output || !output->res)
        return;

    m_level = level;
    m_muted = muted;
    m_dark = dark;

    // Centered on the output (its overlay layer).
    setParent(&output->res->layers[Scene::Overlay]);
    layout().setPosition(YGEdgeLeft, (output->rect().width() - Size) / 2);
    layout().setPosition(YGEdgeTop, (output->rect().height() - Size) / 2);
    setVisible(true);

    // Look.
    const CZColorScheme scheme { dark ? CZColorScheme::Dark : CZColorScheme::Light };
    m_blur.setColorScheme(scheme);
    const SkColor fg { dark ? SkColor(0xE6FFFFFF) : SkColor(0xCC000000) };
    const SkColor empty { dark ? SkColor(0x33FFFFFF) : SkColor(0x26000000) };

    const bool silent { muted || level <= 0.f };
    m_icon.setIconFromName(silent ? "volume_off" : level < 0.34f ? "volume_mute" : level < 0.67f ? "volume_down" : "volume_up");
    m_icon.setColor(fg);

    const Int32 filled { silent ? 0 : Int32(std::lround(level * Int32(m_segments.size()))) };

    for (Int32 i = 0; i < Int32(m_segments.size()); i++)
        m_segments[i]->setColor(CZAdaptiveColor(i < filled ? fg : empty));

    // Hidden a while after the last change.
    m_hideTimer.start(VisibleMs);
}

void VolumeHUD::hide() noexcept
{
    // Out of the scene until shown again.
    setVisible(false);
    setParent(nullptr);
}
