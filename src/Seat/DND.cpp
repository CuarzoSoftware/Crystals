#include <Seat/DND.h>
#include <Effects/DNDReturnAnimation.h>
#include <Louvre/Roles/LDNDIconRole.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Louvre/Cursor/LCursor.h>
#include <Core/CZTimer.h>

void DND::startDragRequest()
{
    LDND::startDragRequest();
    m_startCursor = cursor()->pos();
    m_iconImage.reset();

    // Shelf keeps a dragged app in the slot it was moved to, and flies it there by itself.
    LSurface *from { origin() };

    if (from && from->parent())
        from = from->topmostParent();

    m_returnsByItself = from && from->layerRole() && from->layerRole()->scope() == "cz-shelf-dock";
}

void DND::dropped()
{
    // Snapshot the icon before it's unmapped: it's animated back if the drop isn't taken.
    m_iconImage.reset();
    m_dropCursor = cursor()->pos();

    if (icon() && icon()->surface() && icon()->surface()->mapped())
    {
        LSurface *surface { icon()->surface() };
        m_iconImage = surface->image();
        m_iconRect = SkRect::Make(SkIRect::MakePtSize(surface->rolePos(), surface->size()));

        // The dock flies the app into its slot by itself, starting from here: keep the icon a moment
        // so there's no frame without it while its own copy shows up (Shelf's DOCK_DROP_FLY_HOLD_MS).
        if (m_returnsByItself)
            DNDReturnAnimation::Hold(m_iconImage, m_iconRect, 80);

        // A rejected drop uses it right away (cancelled() runs within drop()): an accepted one
        // shouldn't keep the client's buffer referenced.
        CZTimer::OneShot(0, [this](CZTimer *){ m_iconImage.reset(); });
    }

    LDND::dropped();
}

void DND::cancelled()
{
    // Cancelled without a drop (e.g. Escape, or the client cancelled): the icon is still there.
    if (!m_iconImage && icon() && icon()->surface() && icon()->surface()->mapped())
    {
        LSurface *surface { icon()->surface() };
        m_iconImage = surface->image();
        m_iconRect = SkRect::Make(SkIRect::MakePtSize(surface->rolePos(), surface->size()));
        m_dropCursor = cursor()->pos();
    }

    // Back to where the icon started (it followed the cursor, keeping its offset to it).
    if (m_iconImage && !m_returnsByItself)
        DNDReturnAnimation::Start(m_iconImage, m_iconRect,
            SkPoint::Make(m_iconRect.x() + m_startCursor.x() - m_dropCursor.x(),
                          m_iconRect.y() + m_startCursor.y() - m_dropCursor.y()));

    m_iconImage.reset();
    LDND::cancelled();
}
