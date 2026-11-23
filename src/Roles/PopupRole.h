#ifndef POPUPROLE_H
#define POPUPROLE_H

#include <Louvre/Roles/LPopupRole.h>
#include <Core/Types.h>

/**
 * @brief Popups (menus, tooltips...) kept within the available geometry of the screen under the
 *        cursor: not over the top bar nor the dock (their exclusive zones).
 */
class PopupRole final : public LPopupRole
{
public:
    PopupRole(const void *params) noexcept;
    void configureRequest() override;
};

#endif // POPUPROLE_H
