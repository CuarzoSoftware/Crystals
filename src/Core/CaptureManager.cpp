#include <Core/CaptureManager.h>
#include <Core/Crystals.h>
#include <Roles/ToplevelRole.h>
#include <Louvre/Manager/LImageCaptureSession.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Roles/LLayerRole.h>
#include <algorithm>
#include <cmath>

static constexpr const char *DockScope { "cz-shelf-dock" };

static bool IsDockClient(LClient *client) noexcept
{
    if (!client)
        return false;

    for (LSurface *surface : GetCompositor()->surfaces())
        if (surface->client() == client && surface->layerRole() && surface->layerRole()->scope() == DockScope)
            return true;

    return false;
}

bool CaptureManager::IsThumbnail(const LImageCaptureSession &session) noexcept
{
    auto *manager { static_cast<CaptureManager*>(imageCaptureManager()) };
    return manager && manager->m_thumbnails.contains(&session);
}

SkISize CaptureManager::ThumbnailSize(LToplevelRole &toplevel) noexcept
{
    auto &tl { static_cast<ToplevelRole&>(toplevel) };
    SkISize size { tl.windowGeometry().size() };

    if (size.isEmpty())
        size = tl.surface()->size();

    size.fHeight += tl.titlebarHeight();

    if (size.isEmpty())
        return { 1, 1 };

    // Fit within ThumbnailMaxSide (never larger than the window at its scale)
    const Float32 maxScale { Float32(std::max(1, tl.surface()->scale())) };
    const Float32 s { std::min(maxScale, Float32(ThumbnailMaxSide) / Float32(std::max(size.width(), size.height()))) };
    return { std::max(1, Int32(std::lround(size.width() * s))), std::max(1, Int32(std::lround(size.height() * s))) };
}

void CaptureManager::sessionRequest(LImageCaptureSession *session)
{
    if (session->sourceType() == LImageCaptureSourceType::ForeignToplevel && !session->isCursorSession() &&
        session->toplevel() && IsDockClient(session->client()))
    {
        m_thumbnails.insert(session);
        session->setBufferSize(ThumbnailSize(*session->toplevel()));
    }

    session->accept();
}

void CaptureManager::sessionDestroyed(LImageCaptureSession *session)
{
    m_thumbnails.erase(session);
}

void CaptureManager::ToplevelChanged(LToplevelRole &toplevel) noexcept
{
    auto *manager { imageCaptureManager() };

    if (!manager)
        return;

    // A copy: capturing may change the sessions
    const auto sessions { manager->sessions() };

    for (auto *session : sessions)
        if (session->toplevel() == &toplevel && !session->isCursorSession() && session->state() == LImageCaptureSession::Accepted)
            session->addDamage(SkRegion(SkIRect::MakeSize(session->bufferSize())));
}
