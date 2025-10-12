#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <Louvre/Seat/LKeyboard.h>
#include <Core/Types.h>

class Keyboard final : public LKeyboard
{
public:
    using LKeyboard::LKeyboard;

    void keyEvent(const CZKeyboardKeyEvent &e) override;
};

#endif // KEYBOARD_H
