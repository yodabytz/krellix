#pragma once
#include <QString>

inline QString processBaseName(QString name)
{
    // Preserve bracketed kernel-thread names; only presentation is shortened.
    if (name.startsWith(QLatin1Char('[')) && name.endsWith(QLatin1Char(']')))
        return name;
    const auto dot = name.indexOf(QLatin1Char('.'), name.startsWith(QLatin1Char('.')) ? 1 : 0);
    if (dot > 0) name.truncate(dot);
    return name;
}
