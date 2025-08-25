#ifndef LOG_H
#define LOG_H

#include <CZ/CZLogger.h>

#define Log LogGet()

const CZ::CZLogger &LogGet() noexcept;

#endif // LOG_H
