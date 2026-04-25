#include "MonitorBase.h"

MonitorBase::MonitorBase(Theme *theme, QObject *parent)
    : QObject(parent)
    , m_theme(theme)
{
    Q_ASSERT(m_theme);
}

MonitorBase::~MonitorBase() = default;
