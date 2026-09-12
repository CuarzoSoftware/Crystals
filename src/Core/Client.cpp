#include <Core/Client.h>
#include <Core/Crystals.h>
#include <Core/Types.h>

void Client::setActiveHeavenClient() noexcept
{
    if (auto heaven = GetCompositor()->heaven)
    {
        pid_t pid {};
        uid_t uid {};
        gid_t gid {};
        credentials(&pid, &uid, &gid);
        heaven->setActiveClient(dbusId, pid, uid, gid);
    }
}
