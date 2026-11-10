#ifndef TOPLEVELROLE_H
#define TOPLEVELROLE_H

#include <Louvre/Roles/LToplevelRole.h>
#include <Core/Types.h>

class ToplevelRole : public LToplevelRole
{
public:
    using LToplevelRole::LToplevelRole;

    void minimizedChanged() override;
    void appIdChanged() override;
    void stateChanged(CZBitset<Changes> changes, const State &prev) override;
};

#endif // TOPLEVELROLE_H
