#ifndef VOLUMEHUD_H
#define VOLUMEHUD_H

#include <Core/Types.h>
#include <AK/Nodes/AKContainer.h>
#include <AK/Nodes/AKFontIcon.h>
#include <AK/Nodes/AKSolidColor.h>
#include <AK/Effects/AKBackgroundBlurEffect.h>
#include <Core/CZTimer.h>
#include <array>

/**
 * @brief The volume overlay shown by the volume keys (see Volume), like macOS's.
 *
 * A rounded square with a blurred background, centered on the output with the cursor: a speaker
 * icon (its waves follow the level, crossed out when muted or silent) and the level as 16 segments.
 *
 * It stays while it keeps being shown (show()), then is hidden (removed from the scene) after a
 * short delay.
 */
class VolumeHUD final : public AKContainer
{
public:
    VolumeHUD() noexcept;

    /// Shows @p level (0 to 1) on the output with the cursor, with the light or @p dark look.
    void show(float level, bool muted, bool dark) noexcept;

private:
    void hide() noexcept;

    AKBackgroundBlurEffect m_blur;
    AKFontIcon m_icon { "volume_up", 104, this };
    AKContainer m_bar { YGFlexDirectionRow, false, this };
    std::array<std::unique_ptr<AKSolidColor>, 16> m_segments;

    float m_level { 0.f };
    bool m_muted { false }, m_dark { false };

    CZTimer m_hideTimer;
};

#endif // VOLUMEHUD_H
