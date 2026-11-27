#include <Core/Events/CZKeyboardKeyEvent.h>
#include <Core/CZCore.h>
#include <Seat/Keyboard.h>
#include <Core/Volume.h>
#include <Scene/Scene.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Seat/LOutput.h>
#include <AK/AKApp.h>
#include <Decorations/SSD.h>
#include <Core/Workspaces.h>
#include <Effects/Expose.h>
#include <Effects/ShowDesktop.h>
#include <Core/SessionLock.h>
#include <Core/Screenshot.h>

void Keyboard::keyEvent(const CZKeyboardKeyEvent &e)
{
    // Volume keys: taken by the compositor (see Volume), never sent to clients.
    if (Volume::Get().keyEvent(e.code, e.isPressed))
        return;

    // Locked: only to the lock surface (no shortcuts, see SessionLockManager)
    if (SessionLockManager::IsLocked())
    {
        LKeyboard::keyEvent(e);
        return;
    }

    // Print Screen, Super+Shift+3: screenshot to the desktop folder
    if (Screenshot::KeyEvent(e))
        return;

    // Ctrl+Super+Up/Down (Exposé), Escape while it's shown, Ctrl+Super+Left/Right (workspaces),
    // Super+D (Show Desktop): never sent to clients
    if (Expose::KeyEvent(e) || Workspaces::KeyEvent(e) || ShowDesktop::KeyEvent(e))
        return;

    LKeyboard::keyEvent(e);
    SSD::KeyEvent(e.code); // Alt: the green buttons maximize instead

    if (isKeyCodePressed(KEY_LEFTMETA) && e.code == KEY_M && e.isPressed == false && cursor()->output())
    {
        auto *o { cursor()->output() };
        std::shared_ptr<LOutputMode> mode {};

        for (auto m = o->modes().begin(); m != o->modes().end(); m++)
        {
            if (*m == o->currentMode())
            {
                if (*m == cursor()->output()->modes().back())
                    mode = o->modes().front();
                else
                    mode = *std::next(m);

                break;
            }
        }

        o->setMode(mode);
    }

    CZCore::Get()->sendEvent(e, *AKApp::Get());
}

void Keyboard::focusChanged()
{
    LKeyboard::focusChanged();

    // Locked: never another client (e.g. a window activated meanwhile), it'd get what's typed (the
    // password): back to the lock surface, or nothing (the lock client died)
    if (SessionLockManager::IsLocked())
    {
        if (!SessionLockManager::IsLockClientSurface(focus()))
            if (LSurface *target { SessionLockManager::FocusSurface() }; target != focus())
                setFocus(target);

        return;
    }

    if (!focus() || focus()->toplevel()) return;
    auto *parent { focus()->topmostParent() };
    if (!parent || !parent->toplevel()) return;
    auto *tl { parent->toplevel() };
    tl->configureState(tl->pendingConfiguration().windowState | CZWinActivated);
}
