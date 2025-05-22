#ifndef APPLICATION_H
#define APPLICATION_H

#include <AK/AKApplication.h>
#include <core/Global.h>
#include <ui/Scene.h>

class Application final : public AKApplication
{
public:
    Application() noexcept;
    ~Application();

    Scene scene;
private:
    wl_event_source *eventSource;
};

#endif // APPLICATION_H
