#ifndef CAPTUREMANAGER_H
#define CAPTUREMANAGER_H

#include <Louvre/Manager/LImageCaptureManager.h>
#include <Core/Types.h>
#include <unordered_set>

/**
 * @brief Image capture sessions (ext-image-copy-capture).
 *
 * Every session is accepted. Toplevel captures by Shelf's dock (minimized window thumbnails, see
 * MForeignToplevel::captureImage()) are thumbnails: a small version of the window, title bar
 * included, rendered by ToplevelRole::imageCaptureFrameRequest().
 */
class CaptureManager final : public LImageCaptureManager
{
public:
    using LImageCaptureManager::LImageCaptureManager;

    /// Longest side of a thumbnail (buffer pixels).
    static constexpr Int32 ThumbnailMaxSide { 384 };

    /// Whether @p session captures a dock thumbnail.
    static bool IsThumbnail(const LImageCaptureSession &session) noexcept;

    /// The thumbnail buffer size of @p toplevel (its window, title bar included).
    static SkISize ThumbnailSize(LToplevelRole &toplevel) noexcept;

    /// Redraws the captures of @p toplevel (e.g. its background blur changed, see Output::paintGL()).
    static void ToplevelChanged(LToplevelRole &toplevel) noexcept;

    void sessionRequest(LImageCaptureSession *session) override;
    void sessionDestroyed(LImageCaptureSession *session) override;

private:
    std::unordered_set<const LImageCaptureSession*> m_thumbnails;
};

#endif // CAPTUREMANAGER_H
