#include <Core/Screenshot.h>
#include <Core/Log.h>
#include <Seat/Output.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Ream/RImage.h>
#include <Ream/RMatrixUtils.h>
#include <CZ/skia/core/SkBitmap.h>
#include <CZ/skia/core/SkCanvas.h>
#include <CZ/skia/core/SkImage.h>
#include <CZ/skia/core/SkStream.h>
#include <CZ/skia/encode/SkPngEncoder.h>
#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <ctime>
#include <cstdlib>

namespace fs = std::filesystem;

namespace
{
    // Outputs to capture on their next frame (painted in their own threads) → file name base
    std::mutex s_mutex;
    std::unordered_map<const Output*, std::string> s_pending;

    // Whether the shortcut's key was taken when pressed (its release is taken too)
    UInt32 s_takenKey { 0 };

    // XDG_DESKTOP_DIR (user-dirs.dirs), ~/Desktop if unset (like Desk)
    fs::path DesktopDir() noexcept
    {
        const char *homeEnv { getenv("HOME") };

        if (!homeEnv || homeEnv[0] != '/')
            return {};

        const fs::path home { homeEnv };
        fs::path configHome { home / ".config" };

        if (const char *env { getenv("XDG_CONFIG_HOME") }; env && env[0] == '/')
            configHome = env;

        const std::string prefix { "XDG_DESKTOP_DIR=" };
        std::ifstream in { configHome / "user-dirs.dirs" };
        std::string line;
        fs::path dir;

        while (std::getline(in, line))
        {
            if (!line.starts_with(prefix))
                continue;

            std::string value { line.substr(prefix.size()) };

            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);

            if (value.starts_with("$HOME"))
                dir = home / value.substr(value.size() > 5 && value[5] == '/' ? 6 : 5);
            else if (!value.empty() && value.front() == '/')
                dir = value;
        }

        // Pointing at $HOME means "disabled"
        if (dir.empty() || dir == home)
            dir = home / "Desktop";

        return dir;
    }

    // "<base>.png", or "<base> (n).png" if taken
    fs::path FreePath(const fs::path &dir, const std::string &base) noexcept
    {
        std::error_code ec;
        fs::path path { dir / (base + ".png") };

        for (int n { 2 }; fs::exists(path, ec); n++)
            path = dir / std::format("{} ({}).png", base, n);

        return path;
    }

    void Save(SkBitmap bitmap, std::string base) noexcept
    {
        const fs::path dir { DesktopDir() };

        if (dir.empty())
        {
            Log(CZError, CZLN, "Screenshot not saved: HOME isn't set");
            return;
        }

        std::error_code ec;
        fs::create_directories(dir, ec);

        // Written under a temporary name: the desktop never shows a partial file
        const fs::path path { FreePath(dir, base) };
        const fs::path tmp { dir / ("." + path.filename().string() + ".part") };

        bool ok { false };
        {
            SkFILEWStream stream { tmp.c_str() };
            SkPngEncoder::Options options;
            options.fZLibLevel = 3; // Fast: screenshots are big, and the size barely changes
            ok = stream.isValid() && SkPngEncoder::Encode(&stream, bitmap.pixmap(), options);
        }

        if (ok)
            fs::rename(tmp, path, ec);

        if (!ok || ec)
        {
            fs::remove(tmp, ec);
            Log(CZError, CZLN, "Screenshot not saved to {}", path.string());
            return;
        }

        Log(CZInfo, CZLN, "Screenshot saved to {}", path.string());
    }
}

bool Screenshot::KeyEvent(const CZKeyboardKeyEvent &e) noexcept
{
    // Its release (after a press taken)
    if (!e.isPressed)
    {
        if (s_takenKey && e.code == s_takenKey)
        {
            s_takenKey = 0;
            return true;
        }

        return false;
    }

    bool shortcut { e.code == KEY_SYSRQ || e.code == KEY_PRINT };

    if (!shortcut && e.code == KEY_3)
    {
        auto *kb { seat()->keyboard() };
        const bool super { kb->isKeyCodePressed(KEY_LEFTMETA) || kb->isKeyCodePressed(KEY_RIGHTMETA) };
        const bool shift { kb->isKeyCodePressed(KEY_LEFTSHIFT) || kb->isKeyCodePressed(KEY_RIGHTSHIFT) };
        shortcut = super && shift;
    }

    if (!shortcut)
        return false;

    s_takenKey = e.code;
    Take();
    return true;
}

void Screenshot::Take() noexcept
{
    const auto &outputs { compositor()->outputs() };

    if (outputs.empty())
        return;

    char time[64];
    const std::time_t now { std::time(nullptr) };
    std::tm local {};
    localtime_r(&now, &local);
    std::strftime(time, sizeof(time), "Screenshot %Y-%m-%d at %H.%M.%S", &local);

    {
        std::lock_guard lock { s_mutex };

        for (size_t i = 0; i < outputs.size(); i++)
            s_pending[static_cast<Output*>(outputs[i])] = i == 0 ? std::string(time) : std::format("{} ({})", time, i + 1);
    }

    for (auto *o : outputs)
        o->repaint();
}

void Screenshot::Capture(Output &output) noexcept
{
    std::string base;

    {
        std::lock_guard lock { s_mutex };

        if (s_pending.empty())
            return;

        auto it { s_pending.find(&output) };

        if (it == s_pending.end())
            return;

        base = std::move(it->second);
        s_pending.erase(it);
    }

    auto image { output.image() };

    if (!image)
        return;

    // Read in buffer orientation
    const SkISize size { image->size() };
    RFormat format { DRM_FORMAT_ARGB8888 };
    SkColorType colorType { kBGRA_8888_SkColorType };

    if (!image->readFormats().contains(format))
    {
        format = DRM_FORMAT_ABGR8888;
        colorType = kRGBA_8888_SkColorType;
    }

    SkBitmap raw;

    if (!raw.tryAllocPixels(SkImageInfo::Make(size, colorType, kOpaque_SkAlphaType)))
    {
        Log(CZError, CZLN, "Screenshot: out of memory");
        return;
    }

    const bool read { image->readPixels({
        .offset = { 0, 0 },
        .stride = UInt32(raw.rowBytes()),
        .pixels = static_cast<UInt8*>(raw.getPixels()),
        .region = SkRegion(SkIRect::MakeSize(size)),
        .format = format }) };

    if (!read)
    {
        Log(CZError, CZLN, "Screenshot: couldn't read the image of output {}", output.name());
        return;
    }

    // Transformed (rotated, flipped): drawn as shown, at the buffer's resolution
    if (output.transform() != CZTransform::Normal)
    {
        const SkRect viewport { SkRect::Make(output.rect()) };
        const SkISize shotSize { CZ::Is90Transform(output.transform()) ? SkISize::Make(size.height(), size.width()) : size };
        SkBitmap shot;

        if (!shot.tryAllocPixels(SkImageInfo::Make(shotSize, colorType, kOpaque_SkAlphaType)))
        {
            Log(CZError, CZLN, "Screenshot: out of memory");
            return;
        }

        // Shot pixels → logical → buffer pixels (as rendered, see Output::paintGL())
        SkMatrix shotToBuffer { SkMatrix::Translate(viewport.x(), viewport.y()) };
        shotToBuffer.preScale(viewport.width() / shotSize.width(), viewport.height() / shotSize.height());
        shotToBuffer.postConcat(RMatrixUtils::VirtualToImage(output.transform(), viewport, SkRect::Make(size)));

        SkMatrix bufferToShot;

        if (!shotToBuffer.invert(&bufferToShot))
            return;

        SkCanvas canvas { shot };
        canvas.concat(bufferToShot);
        canvas.drawImage(raw.asImage(), 0, 0);
        raw = std::move(shot);
    }

    std::thread(Save, std::move(raw), std::move(base)).detach();
}
