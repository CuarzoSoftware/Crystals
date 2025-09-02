#ifndef LOG_H
#define LOG_H

#include <CZ/Core/CZLogger.h>

#define Log LogGet()

const CZ::CZLogger &LogGet() noexcept;

#endif // LOG_H
