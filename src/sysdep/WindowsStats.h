#pragma once

#include "NetStat.h"
#include "NetPortStat.h"
#include "ProcStat.h"

namespace WindowsStats {
QList<NetSample> network();
QList<NetPortSample> ports();
ProcInfo processes();
}
