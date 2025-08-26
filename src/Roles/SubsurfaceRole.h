#ifndef SUBSURFACEROLE_H
#define SUBSURFACEROLE_H

#include <Roles/LSubsurfaceRole.h>
#include <Core/Types.h>

class SubsurfaceRole final : public LSubsurfaceRole
{
public:
    SubsurfaceRole(const void *params) noexcept;
    void localPosChanged() noexcept;
    void placedAbove(LSurface *sibling) noexcept;
    void placedBelow(LSurface *sibling) noexcept;
};

#endif // SUBSURFACEROLE_H
