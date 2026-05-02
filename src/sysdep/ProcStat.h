#pragma once

struct ProcInfo {
    int processes = 0;
    int users = 0;
};

class ProcStat
{
public:
    static ProcInfo read();

    using ReadFn = ProcInfo (*)();
    static void setReadOverride(ReadFn fn);
};
