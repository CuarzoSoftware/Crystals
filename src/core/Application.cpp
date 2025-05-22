#include <core/Application.h>
#include <core/Compositor.h>

Application::Application() noexcept
{
    // Integrate Kay event loop into Louvre
    eventSource = wl_event_loop_add_fd(
        G::compositor()->eventLoop(),
        fd(),
        WL_EVENT_READABLE,
        [](int /*fd*/, uint32_t /*mask*/, void *data) {
            static_cast<AKApplication*>(data)->processLoop(0);
            return 0;
        },
        this);
}

Application::~Application()
{
    wl_event_source_remove(eventSource);
}
