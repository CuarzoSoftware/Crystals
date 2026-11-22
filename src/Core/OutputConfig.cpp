#include <Core/OutputConfig.h>
#include <Core/Log.h>
#include <Core/CZTimer.h>
#include <Seat/Seat.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Seat/LOutputMode.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>
#include <pwd.h>
#include <unistd.h>

namespace fs = std::filesystem;

/* ------------------------------------------ The file ----------------------------------------- */

namespace
{
using Section = std::vector<std::pair<std::string, std::string>>;

// A minimal INI file, in order (sections and keys as written)
struct Ini
{
    std::vector<std::pair<std::string, Section>> sections;

    const Section *find(const std::string &name) const noexcept
    {
        for (const auto &[n, s] : sections)
            if (n == name)
                return &s;

        return nullptr;
    }

    // Replaced (or added at the end)
    Section &reset(const std::string &name) noexcept
    {
        for (auto &[n, s] : sections)
            if (n == name)
                return s = {};

        return sections.emplace_back(name, Section{}).second;
    }
};

std::string Trim(const std::string &s) noexcept
{
    const auto b { s.find_first_not_of(" \t\r") };

    if (b == std::string::npos)
        return "";

    return s.substr(b, s.find_last_not_of(" \t\r") - b + 1);
}

std::string Get(const Section *s, const std::string &key) noexcept
{
    if (s)
        for (const auto &[k, v] : *s)
            if (k == key)
                return v;

    return "";
}

Ini Load() noexcept
{
    Ini ini;
    std::ifstream in { OutputConfig::FilePath() };
    std::string line;
    Section *current {};

    while (std::getline(in, line))
    {
        line = Trim(line);

        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        if (line.front() == '[' && line.back() == ']')
        {
            current = &ini.sections.emplace_back(line.substr(1, line.size() - 2), Section{}).second;
            continue;
        }

        const auto eq { line.find('=') };

        if (current && eq != std::string::npos)
            current->emplace_back(Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
    }

    return ini;
}

void Write(const Ini &ini) noexcept
{
    const fs::path path { OutputConfig::FilePath() };
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // A temporary file first: a crash can't leave a truncated one
    const fs::path tmp { path.string() + ".tmp" };
    {
        std::ofstream out { tmp, std::ios::trunc };

        if (!out)
        {
            Log(CZError, CZLN, "Failed to write the displays configuration {}", tmp.string());
            return;
        }

        out << "# The displays' configuration, written by Crystals when one is applied (e.g. from System Settings).\n"
               "# [Setup <displays>]: a set of displays connected at once, [Setup <displays> / <display>]: how each\n"
               "# one was configured in it, [Display <display>]: its last configuration (for new setups).\n"
               "# Mode=<width>x<height>@<refresh in mHz>, Transform=normal|90|180|270|flipped|flipped-90|flipped-180|flipped-270\n";

        for (const auto &[name, section] : ini.sections)
        {
            out << "\n[" << name << "]\n";

            for (const auto &[k, v] : section)
                out << k << '=' << v << '\n';
        }
    }

    fs::rename(tmp, path, ec);

    if (ec)
        Log(CZError, CZLN, "Failed to save the displays configuration {}: {}", path.string(), ec.message());
    else
        Log(CZInfo, CZLN, "Displays configuration saved to {}", path.string());
}

/* ------------------------------------------ Displays ----------------------------------------- */

const char *TransformNames[] { "normal", "90", "180", "270", "flipped", "flipped-90", "flipped-180", "flipped-270" };

// Section names can't hold these
std::string Clean(const std::string &text) noexcept
{
    std::string s { Trim(text) };

    for (char &c : s)
        if (c == '[' || c == ']' || c == '/' || c == '+' || c == '\n' || c == '=')
            c = '_';

    return s;
}

// The same display on any connector (its connector if it says nothing about itself)
std::string Id(LOutput *output) noexcept
{
    std::string id { Clean(output->manufacturer()) };

    for (const auto &part : { output->model(), output->serialNumber() })
        if (const std::string p { Clean(part) }; !p.empty())
            id += (id.empty() ? "" : " ") + p;

    return id.empty() ? "Connector " + Clean(output->name()) : id;
}

std::vector<LOutput*> Connected(LOutput *gone) noexcept
{
    std::vector<LOutput*> outputs;

    for (LOutput *o : seat()->outputs())
        if (o != gone && !o->isNonDesktop())
            outputs.push_back(o);

    return outputs;
}

std::string SetupName(const std::vector<LOutput*> &outputs) noexcept
{
    std::vector<std::string> ids;

    for (LOutput *o : outputs)
        ids.push_back(Id(o));

    std::sort(ids.begin(), ids.end());
    std::string name { "Setup " };

    for (size_t i = 0; i < ids.size(); i++)
        name += (i ? " + " : "") + ids[i];

    return name;
}

bool IsEnabled(LOutput *output) noexcept
{
    const auto &outputs { compositor()->outputs() };
    return std::find(outputs.begin(), outputs.end(), output) != outputs.end();
}

std::string ModeText(const LOutputMode &mode) noexcept
{
    return std::format("{}x{}@{}", mode.size().width(), mode.size().height(), mode.refreshRate());
}

std::string ScaleText(Float32 scale) noexcept
{
    std::string text { std::format("{:.4f}", scale) };

    while (text.back() == '0')
        text.pop_back();

    if (text.back() == '.')
        text.pop_back();

    return text;
}

// Everything known about it, and how it's configured
void Fill(Section &s, LOutput *output) noexcept
{
    s.emplace_back("Connector", output->name());
    s.emplace_back("Description", output->description());
    s.emplace_back("Make", output->manufacturer());
    s.emplace_back("Model", output->model());
    s.emplace_back("SerialNumber", output->serialNumber());
    s.emplace_back("PhysicalSize", std::format("{}x{}", output->mmSize().width(), output->mmSize().height()));
    s.emplace_back("DPI", std::to_string(output->dpi()));

    std::string modes;

    for (const auto &mode : output->modes())
        modes += (modes.empty() ? "" : ";") + ModeText(*mode);

    s.emplace_back("Modes", modes);

    if (const auto preferred { output->preferredMode() })
        s.emplace_back("PreferredMode", ModeText(*preferred));

    s.emplace_back("Enabled", IsEnabled(output) ? "true" : "false");

    if (const auto mode { output->currentMode() })
        s.emplace_back("Mode", ModeText(*mode));

    s.emplace_back("Position", std::format("{},{}", output->pos().x(), output->pos().y()));
    s.emplace_back("Transform", TransformNames[std::clamp(Int32(output->transform()), 0, 7)]);
    s.emplace_back("Scale", ScaleText(output->fractionalScale()));
}

// How to configure a display
struct Plan
{
    LOutput *output;
    bool enabled { true };
    std::shared_ptr<LOutputMode> mode;
    SkIPoint pos {};
    CZTransform transform { CZTransform::Normal };
    Float32 scale { 1.f };
};

// The saved one, or the closest (same size), or none
std::shared_ptr<LOutputMode> FindMode(LOutput *output, const std::string &text) noexcept
{
    Int32 w { 0 }, h { 0 };
    UInt32 refresh { 0 };

    if (std::sscanf(text.c_str(), "%dx%d@%u", &w, &h, &refresh) != 3)
        return nullptr;

    std::shared_ptr<LOutputMode> best;

    for (const auto &mode : output->modes())
    {
        if (mode->size() != SkISize::Make(w, h))
            continue;

        const auto diff = [refresh](const LOutputMode &m){ return std::abs(Int64(m.refreshRate()) - Int64(refresh)); };

        if (!best || diff(*mode) < diff(*best))
            best = mode;
    }

    return best;
}

// Like Louvre (see LOutput::size()): fractional scales render at the next integer one, downscaled
SkISize LogicalSize(SkISize size, CZTransform transform, Float32 fractional) noexcept
{
    fractional = std::max(fractional, 0.25f);
    const Float32 integer { std::ceil(fractional) };
    Float32 w { Float32(size.width()) }, h { Float32(size.height()) };

    if (fractional != integer)
    {
        w = std::round(w * integer / fractional);
        h = std::round(h * integer / fractional);
    }

    if (CZ::Is90Transform(transform))
        std::swap(w, h);

    return SkISize::Make(Int32(std::round(w / integer)), Int32(std::round(h / integer)));
}

// From the file: @return whether the setup of @p outputs was saved (else each display's last one)
bool MakePlan(const std::vector<LOutput*> &outputs, std::vector<Plan> &plan) noexcept
{
    const Ini ini { Load() };
    const std::string setup { SetupName(outputs) };
    bool exact { ini.find(setup) != nullptr };

    for (LOutput *o : outputs)
        if (!ini.find(setup + " / " + Id(o)))
            exact = false;

    for (LOutput *o : outputs)
    {
        const Section *s { exact ? ini.find(setup + " / " + Id(o)) : ini.find("Display " + Id(o)) };
        Plan &p { plan.emplace_back(Plan{ .output = o }) };
        p.mode = s ? FindMode(o, Get(s, "Mode")) : nullptr;

        if (!p.mode)
            p.mode = o->preferredMode();

        p.scale = o->dpi() >= 200 ? 2.f : 1.f; // Like Louvre when unknown

        if (const std::string scale { Get(s, "Scale") }; !scale.empty())
            if (const Float32 value { std::strtof(scale.c_str(), nullptr) }; value >= 0.25f && value <= 8.f)
                p.scale = value;

        const std::string transform { Get(s, "Transform") };

        for (Int32 i = 0; i < 8; i++)
            if (transform == TransformNames[i])
                p.transform = CZTransform(i);

        if (exact)
        {
            p.enabled = Get(s, "Enabled") != "false";
            std::sscanf(Get(s, "Position").c_str(), "%d,%d", &p.pos.fX, &p.pos.fY);
        }
    }

    // At least one in use
    if (!plan.empty() && std::none_of(plan.begin(), plan.end(), [](const Plan &p){ return p.enabled; }))
        plan.front().enabled = true;

    // Not saved together: left to right
    if (!exact)
    {
        Int32 x { 0 };

        for (Plan &p : plan)
        {
            p.pos = SkIPoint::Make(x, 0);

            if (p.mode)
                x += LogicalSize(p.mode->size(), p.transform, p.scale).width();
        }
    }

    return exact;
}

void Apply(const Plan &p) noexcept
{
    if (p.mode)
        p.output->setMode(p.mode);

    p.output->setScale(p.scale);
    p.output->setTransform(p.transform);
    p.output->setPos(p.pos);
}

std::unique_ptr<CZTimer> SaveTimer;
}

/* ------------------------------------------- Public ------------------------------------------ */

fs::path OutputConfig::FilePath() noexcept
{
    fs::path dir;

    if (const char *config { getenv("XDG_CONFIG_HOME") }; config && config[0] == '/')
        dir = config;
    else if (const char *home { getenv("HOME") }; home && home[0] == '/')
        dir = fs::path(home) / ".config";
    else if (const passwd *pw { getpwuid(getuid()) }; pw && pw->pw_dir)
        dir = fs::path(pw->pw_dir) / ".config";

    return dir / "cz-crystals" / "outputs.conf";
}

void OutputConfig::InitializeOutputs() noexcept
{
    // Probably VR headsets, meant to be leased by clients
    for (LOutput *o : seat()->outputs())
        if (o->isNonDesktop())
            o->setLeasable(true);

    std::vector<Plan> plan;
    const bool exact { MakePlan(Connected(nullptr), plan) };
    Log(CZInfo, CZLN, "Displays: {}", exact ? "restoring their saved setup" : "no saved setup, arranged left to right");

    for (const Plan &p : plan)
    {
        Apply(p);

        if (p.enabled)
        {
            compositor()->addOutput(p.output);
            p.output->repaint();
        }
    }
}

bool OutputConfig::Restore(LOutput *gone) noexcept
{
    const auto outputs { Connected(gone) };
    std::vector<Plan> plan;

    if (outputs.empty() || !MakePlan(outputs, plan))
        return false;

    std::vector<LSeat::OutputConfiguration> configurations;

    for (const Plan &p : plan)
        configurations.push_back({ *p.output, p.enabled, p.mode ? p.mode : p.output->currentMode(), p.pos, p.transform, p.scale });

    Log(CZInfo, CZLN, "Displays: restoring the saved setup of the connected ones");
    GetSeat()->configureOutputsRequest(nullptr, configurations);
    return true;
}

void OutputConfig::OutputPlugged(LOutput *output) noexcept
{
    if (output->isNonDesktop())
    {
        output->setLeasable(true);
        return;
    }

    if (Restore())
        return;

    // Its last configuration, at the right end
    std::vector<Plan> plan;
    MakePlan(Connected(nullptr), plan);

    for (Plan &p : plan)
    {
        if (p.output != output)
            continue;

        Int32 right { 0 };

        for (LOutput *o : compositor()->outputs())
            right = std::max(right, o->pos().x() + o->size().width());

        p.pos = SkIPoint::Make(right, 0);
        Apply(p);
        compositor()->addOutput(output);
        compositor()->repaintAllOutputs();
    }
}

void OutputConfig::ScheduleSave() noexcept
{
    if (!SaveTimer)
        SaveTimer = std::make_unique<CZTimer>([](CZTimer*){ Save(); });

    SaveTimer->start(SaveDelayMs);
}

void OutputConfig::Save() noexcept
{
    const auto outputs { Connected(nullptr) };

    if (outputs.empty())
        return;

    Ini ini { Load() };
    const std::string setup { SetupName(outputs) };

    // This setup's sections are written again
    std::erase_if(ini.sections, [&setup](const auto &section){
        return section.first == setup || section.first.starts_with(setup + " / ");
    });

    Section &info { ini.reset(setup) };
    info.emplace_back("Displays", std::to_string(outputs.size()));
    info.emplace_back("Saved", std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())));

    for (LOutput *o : outputs)
    {
        Fill(ini.reset(setup + " / " + Id(o)), o);
        Fill(ini.reset("Display " + Id(o)), o);
    }

    Write(ini);
}
