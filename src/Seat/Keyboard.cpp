#include <Core/Events/CZKeyboardKeyEvent.h>
#include <Core/CZCore.h>
#include <Seat/Keyboard.h>
#include <Scene/Scene.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Roles/LToplevelRole.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Seat/LOutput.h>
#include <AK/AKApp.h>

void Keyboard::keyEvent(const CZKeyboardKeyEvent &e)
{
    LKeyboard::keyEvent(e);

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
    if (!focus() || focus()->toplevel()) return;
    auto *parent { focus()->topmostParent() };
    if (!parent || !parent->toplevel()) return;
    auto *tl { parent->toplevel() };
    tl->configureState(tl->pendingConfiguration().windowState | CZWinActivated);
}
