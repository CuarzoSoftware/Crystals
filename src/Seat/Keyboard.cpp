#include <Core/Events/CZKeyboardKeyEvent.h>
#include <Core/CZCore.h>
#include <Seat/Keyboard.h>
#include <Scene/Scene.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <AK/AKApp.h>

void Keyboard::keyEvent(const CZKeyboardKeyEvent &e)
{
    LKeyboard::keyEvent(e);
    CZCore::Get()->sendEvent(e, *AKApp::Get());
}

void Keyboard::focusChanged()
{
    LKeyboard::focusChanged();
    if (!focus() || focus()->toplevel()) return;
    auto *parent { focus()->topmostParent() };
    if (!parent || !parent->toplevel()) return;
    auto *tl { parent->toplevel() };
    tl->configureState(tl->pendingConfiguration().windowState | CZWinActivated);
}
