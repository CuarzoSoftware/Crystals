#ifndef RESIZEBLACKOUT_H
#define RESIZEBLACKOUT_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZTimer.h>
#include <Core/CZLinearAnimation.h>
#include <AK/Nodes/AKSolidColor.h>
#include <memory>

/**
 * @brief Black over an output that changed size, until its desktop shell caught up.
 *
 * When an output is resized (e.g. the nested window of the Wayland backend, rotated, another
 * mode), its layer shell surfaces (the desktop, the docks, the top bars, see Crystals::IsDesk()
 * and Crystals::IsShell()) are configured with their new size, and briefly shown at the old one.
 * The output stays black (above everything but the lock screen) until each of them committed the
 * size it was configured with (see LayerRole::settled()), or TimeoutMs passed, then the black
 * fades out (FadeMs). Resized again meanwhile: black again.
 *
 * BeginAll() does it for every output at once (an output configuration is being applied, see
 * Seat::configureOutputsRequest()): they stay black until the shells of all of them settled, or
 * the given timeout passed, then fade out together. Outputs enabled meanwhile join it, black until
 * their desktop appears too.
 */
class ResizeBlackout final : public CZObject
{
public:
    static constexpr UInt32 TimeoutMs { 1000 };
    static constexpr UInt32 FadeMs { 250 };
    static constexpr UInt32 ConfigureTimeoutMs { 5000 }; ///< See BeginAll()

    ResizeBlackout(Output &output) noexcept;
    ~ResizeBlackout() noexcept;

    /// The output changed size: black until its shell caught up.
    void begin() noexcept;

    /// Fades out once every shell surface on the output settled (cheap, called before rendering).
    void update() noexcept;

    /// A surface committed a new size: its output's blackout may end.
    static void SurfaceResized(LSurface *surface) noexcept;

    /// Every output black until all their shells settled (or @p timeoutMs passed), then they fade out together.
    static void BeginAll(UInt32 timeoutMs = ConfigureTimeoutMs) noexcept;

    /// A BeginAll() is in progress.
    static bool GroupActive() noexcept { return s_group; }

    /// A new output (initialized during a BeginAll()): black until its desktop appears and the others settled.
    void joinGroup() noexcept;

private:
    bool settled() const noexcept;
    void end() noexcept; // Fades out
    static bool GroupSettled() noexcept;
    static void EndAll() noexcept;
    static inline bool s_group { false };
    static inline std::unique_ptr<CZTimer> s_groupTimeout;
    void placeBlack() noexcept;

    Output &m_output;
    AKSolidColor m_black;
    CZTimer m_timeout;
    CZLinearAnimation m_fade;
    bool m_waiting { false };
    bool m_inGroup { false };   // See BeginAll()
    bool m_needsDesk { false }; // See joinGroup()
};

#endif // RESIZEBLACKOUT_H
