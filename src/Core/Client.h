#ifndef CLIENT_H
#define CLIENT_H

#include <Louvre/LClient.h>
#include <string>

class Client final : public CZ::LClient
{
public:
    Client(const void *params) : LClient(params) {}
    std::string dbusId;
};

#endif // CLIENT_H
