#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Effects/MinimizeAnimation.h>
#include <Core/Client.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Cursor/LCursor.h>
#include <Decorations/SSD.h>
#include <Decorations/SSDTheme.h>
#include <algorithm>

ToplevelRole::ToplevelRole(const void *params) noexcept : LToplevelRole(params) {}

ToplevelRole::~ToplevelRole()
{
    m_ssd.reset();

    if (auto *surf { static_cast<Surface*>(surface()) })
        surf->view.setBottomRadius(0);
}

LToplevelRole::DecorationMode ToplevelRole::wantedDecorationMode() const noexcept
{
    // Ours unless the client prefers its own (clients without xdg-decoration stay client-side).
    return preferredDecorationMode() == ClientSide ? ClientSide : ServerSide;
}

Int32 ToplevelRole::titlebarHeight() const noexcept
{
    return m_ssd ? SSDTheme::TitlebarHeight : 0;
}

SkIPoint ToplevelRole::rolePos() const
{
    return LToplevelRole::rolePos() + SkIPoint::Make(0, titlebarHeight());
}

void ToplevelRole::configureRequest()
{
    LToplevelRole::configureRequest();
    configureDecorationMode(wantedDecorationMode());
}

void ToplevelRole::preferredDecorationModeChanged()
{
    configureDecorationMode(wantedDecorationMode());
}

void ToplevelRole::setMaximizedRequest()
{
    LToplevelRole::setMaximizedRequest();

    // The available area holds the title bar too.
    if (pendingConfiguration().windowState.has(CZWinMaximized) && decorationMode() == ServerSide && exclusiveOutput())
    {
        SkISize size { exclusiveOutput()->availableGeometry().size() };
        size.fHeight -= SSDTheme::TitlebarHeight;
        configureSize(size);
    }
}

void ToplevelRole::titleChanged()
{
    LToplevelRole::titleChanged();

    if (m_ssd)
        m_ssd->setTitle(title());
}

void ToplevelRole::updateDecorations() noexcept
{
    auto *surf { static_cast<Surface*>(surface()) };

    if (!surf)
        return;

    const bool wanted { decorationMode() == ServerSide && !isFullscreen() };

    if (wanted && !m_ssd)
    {
        m_ssd = std::make_unique<SSD>(*this);
        surf->view.setBottomRadius(SSDTheme::BorderRadius);
    }
    else if (!wanted && m_ssd)
    {
        m_ssd.reset();
        surf->view.setBottomRadius(0);
    }
    else if (m_ssd)
        m_ssd->update();

    enforceMinSize();
    surf->view.syncPos();
}

void ToplevelRole::enforceMinSize() noexcept
{
    // Decorated windows smaller than the decorations allow (their shadow corners would overlap) are
    // asked to grow, once per undersized state (some can't, e.g. fixed size dialogs).
    const SkISize size { surface()->size() };
    const SkISize min { SSD::MinSize() };

    if (!m_ssd || size.isEmpty() || (size.width() >= min.width() && size.height() >= min.height()))
    {
        m_minSizeRequested = false;
        return;
    }

    if (m_minSizeRequested || isMaximized() || isFullscreen() || resizeSession().isActive())
        return;

    const SkISize wanted { std::max(size.width(), min.width()), std::max(size.height(), min.height()) };

    if ((maxSize().width() != 0 && maxSize().width() < wanted.width()) ||
        (maxSize().height() != 0 && maxSize().height() < wanted.height()))
        return;

    m_minSizeRequested = true;
    configureSize(wanted);
}

void ToplevelRole::minimizedChanged()
{
    LToplevelRole::minimizedChanged();

    auto *surf { static_cast<Surface*>(surface()) };

    if (surf->mapped())
        MinimizeAnimation::Start(*this, isMinimized());

    surf->view.syncVisibility();
}

void ToplevelRole::appIdChanged()
{
    LToplevelRole::appIdChanged();

    // Keep the bar's view of the active app in sync (e.g. an app id set after activation).
    if (seat()->activeToplevel() == this)
        static_cast<Client*>(client())->setActiveHeavenClient(this);
}

void ToplevelRole::stateChanged(CZBitset<Changes> changes, const State &prev)
{
    LToplevelRole::stateChanged(changes, prev);
    updateDecorations();

    // Activated (new, clicked, or asked by e.g. the dock): it also gets the keyboard focus, which
    // apps (e.g. Qt ones) treat as being the active window.
    if (changes.has(StateChanged) && isActivated() && !prev.windowState.has(CZWinActivated))
    {
        auto *keyboard { seat()->keyboard() };

        if (!keyboard->grab() && !surface()->hasKeyboardFocus())
            keyboard->setFocus(surface());
    }
}
