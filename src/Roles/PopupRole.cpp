#include <Roles/PopupRole.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Roles/LSurface.h>

PopupRole::PopupRole(const void *params) noexcept : LPopupRole(params) {}

void PopupRole::configureRequest()
{
    LOutput *output { cursor()->output() };

    if (output)
    {
        // Slid, flipped or shrunk to fit outside the bars' exclusive zones (the whole screen if
        // nothing is left)
        SkIRect available { output->availableGeometry() }; // Output-local
        available.offset(output->pos().x(), output->pos().y());
        setBounds(available.isEmpty() ? output->rect() : available);
        surface()->sendOutputEnterEvent(output);
    }
    else
        setBounds(SkIRect::MakeEmpty());

    configureRect(calculateUnconstrainedRect());
}
