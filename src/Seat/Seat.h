#ifndef SEAT_H
#define SEAT_H

#include <Core/Types.h>
#include <Louvre/Seat/LSeat.h>

struct libinput_device;

class Seat final : public LSeat
{
public:
    Seat(const void *params) noexcept;
    void configureLibinputDevice(libinput_device *dev) noexcept;
    void configureInputDevices() noexcept;
    /// The session was left (another TTY) or is back: see SessionState.
    void enabledChanged() override;

    void activeToplevelChanged(LToplevelRole *prev) noexcept override;
    void inputDevicePlugged(std::shared_ptr<CZInputDevice> dev) override;

    /// Configured as saved (see OutputConfig::OutputPlugged()).
    void outputPlugged(LOutput *output) override;

    /// Its windows go to the closest output (see Workspaces::Evacuate()), then the saved setup of the
    /// remaining ones is restored, if any (see OutputConfig::Restore()). Not while the session is
    /// left (another TTY): everything is put back once it's back (see SessionState).
    void outputUnplugged(LOutput *output) override;

    /**
     * @brief An output configuration (e.g. from wdisplays): applied as Louvre does, then the windows
     *        move with their outputs and stay inside them, and those of disabled outputs go to the
     *        closest one (see Workspaces::Evacuate()).
     *
     * Every output is black meanwhile, until their shells caught up (see ResizeBlackout::BeginAll()).
     * Applied by a client, it's saved (see OutputConfig::ScheduleSave()).
     */
    bool configureOutputsRequest(LClient *client, const std::vector<OutputConfiguration> &configurations) override;
    void systemBellRequest(LClient *client, LSurface *surface) override;
    void inputEvent(const CZInputEvent &e) noexcept override;

    /// Popups first: they're drawn above everything (see Scene).
    LSurface *surfaceAt(SkIPoint point) const noexcept override;
    CZLogger log;
};

#endif // SEAT_H
