#include <CZ/AK/AKLog.h>

using namespace CZ;

const CZ::CZLogger &LogGet() noexcept
{
    static CZLogger logger { "Crystals", "CZ_CRYSTALS_LOG_LEVEL" };
    return logger;
}
