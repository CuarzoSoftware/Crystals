#ifndef CLIENT_H
#define CLIENT_H

#include <Louvre/LClient.h>
#include <string>

class Client final : public CZ::LClient
{
public:
    Client(const void *params) : LClient(params) {}
    std::string dbusId;

    /// Notifies Heaven that this client is active, along with its credentials and the app id of
    /// its active @p toplevel (nullable).
    void setActiveHeavenClient(CZ::LToplevelRole *toplevel) noexcept;
};

#endif // CLIENT_H
