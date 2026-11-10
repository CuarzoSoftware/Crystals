#ifndef DND_H
#define DND_H

#include <Louvre/Seat/LDND.h>
#include <Core/Types.h>
#include <Ream/RImage.h>
#include <CZ/skia/core/SkRect.h>
#include <memory>

/**
 * @brief Drag & drop sessions: icons of drops nobody takes slide back to where they came from.
 */
class DND final : public LDND
{
public:
    DND(const void *params) noexcept : LDND(params) {}

    void startDragRequest() override;
    void dropped() override;
    void cancelled() override;

private:
    // Captured at the start: the return animation's destination is the icon's start position.
    SkPoint m_startCursor {};

    // Drags from the dock (Shelf) animate their own icon back into place.
    bool m_returnsByItself { false };

    // The icon's last frame, captured on drop() (the client unmaps it right after).
    std::shared_ptr<RImage> m_iconImage;
    SkRect m_iconRect {};
    SkPoint m_dropCursor {};
};

#endif // DND_H
