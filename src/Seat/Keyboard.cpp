#include <Events/CZKeyboardKeyEvent.h>
#include <Core/CZCore.h>
#include <Seat/Keyboard.h>
#include <Scene/Scene.h>
#include <AKApp.h>

void Keyboard::keyEvent(const CZKeyboardKeyEvent &e)
{
    LKeyboard::keyEvent(e);
    CZCore::Get()->sendEvent(e, *AKApp::Get());
}
