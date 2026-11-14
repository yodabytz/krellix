#include "WindowsStats.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <QHostAddress>
#include <QSet>
#include <vector>
#include <cstring>

namespace WindowsStats {
QList<NetSample> network()
{
    QList<NetSample> out;
    MIB_IF_TABLE2 *table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) return out;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto &row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) continue;
        NetSample sample;
        // Stable LUID identity survives display-name changes.
        sample.name = QStringLiteral("if-%1").arg(row.InterfaceLuid.Value);
        sample.alias = QString::fromWCharArray(row.Alias);
        sample.rxBytes = row.InOctets;
        sample.txBytes = row.OutOctets;
        sample.rxPackets = row.InUcastPkts + row.InNUcastPkts;
        sample.txPackets = row.OutUcastPkts + row.OutNUcastPkts;
        out.append(sample);
    }
    FreeMibTable(table);
    return out;
}

ProcInfo processes()
{
    ProcInfo out;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do { ++out.processes; } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    WTS_SESSION_INFOW *sessions = nullptr;
    DWORD count = 0;
    QSet<QString> users;
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        for (DWORD i = 0; i < count; ++i) {
            LPWSTR name = nullptr;
            DWORD size = 0;
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessions[i].SessionId,
                                           WTSUserName, &name, &size)) {
                const QString user = QString::fromWCharArray(name);
                if (!user.isEmpty()) users.insert(user);
                WTSFreeMemory(name);
            }
        }
        WTSFreeMemory(sessions);
    }
    out.users = users.size();
    return out;
}

namespace {
QString state(DWORD value)
{
    switch (value) {
    case MIB_TCP_STATE_ESTAB: return QStringLiteral("01");
    case MIB_TCP_STATE_SYN_SENT: return QStringLiteral("02");
    case MIB_TCP_STATE_SYN_RCVD: return QStringLiteral("03");
    case MIB_TCP_STATE_FIN_WAIT1: return QStringLiteral("04");
    case MIB_TCP_STATE_FIN_WAIT2: return QStringLiteral("05");
    case MIB_TCP_STATE_TIME_WAIT: return QStringLiteral("06");
    case MIB_TCP_STATE_CLOSE_WAIT: return QStringLiteral("08");
    case MIB_TCP_STATE_LAST_ACK: return QStringLiteral("09");
    case MIB_TCP_STATE_LISTEN: return QStringLiteral("0A");
    case MIB_TCP_STATE_CLOSING: return QStringLiteral("0B");
    default: return QStringLiteral("07");
    }
}

template<typename Table, typename Fetch, typename Append>
void readTable(Fetch fetch, Append append)
{
    DWORD size = 0;
    if (fetch(nullptr, &size) != ERROR_INSUFFICIENT_BUFFER) return;
    // Tables may grow between calls; bounded retries and allocation.
    for (int attempt = 0; attempt < 3 && size <= 16 * 1024 * 1024; ++attempt) {
        if (size < sizeof(DWORD)) return;
        std::vector<unsigned char> buffer(size);
        const DWORD result = fetch(buffer.data(), &size);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue;
        if (result != NO_ERROR) return;
        const auto *table = reinterpret_cast<const Table *>(buffer.data());
        const size_t offset = offsetof(Table, table);
        if (buffer.size() < offset || table->dwNumEntries > (buffer.size() - offset) / sizeof(table->table[0])) return;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) append(table->table[i]);
        return;
    }
}
}

QList<NetPortSample> ports()
{
    QList<NetPortSample> out;
    readTable<MIB_TCPTABLE_OWNER_PID>([](void *p, DWORD *n) {
        return GetExtendedTcpTable(p, n, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    }, [&](const MIB_TCPROW_OWNER_PID &row) {
        NetPortSample sample;
        sample.protocol = QStringLiteral("tcp");
        sample.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
        sample.remotePort = ntohs(static_cast<u_short>(row.dwRemotePort));
        if (row.dwRemoteAddr) sample.remoteAddress = QHostAddress(ntohl(row.dwRemoteAddr)).toString();
        sample.state = state(row.dwState);
        out.append(sample);
    });
    readTable<MIB_TCP6TABLE_OWNER_PID>([](void *p, DWORD *n) {
        return GetExtendedTcpTable(p, n, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    }, [&](const MIB_TCP6ROW_OWNER_PID &row) {
        NetPortSample sample;
        sample.protocol = QStringLiteral("tcp");
        sample.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
        sample.remotePort = ntohs(static_cast<u_short>(row.dwRemotePort));
        Q_IPV6ADDR addr{};
        std::memcpy(addr.c, row.ucRemoteAddr, sizeof(addr.c));
        QHostAddress host(addr);
        if (row.dwRemoteScopeId) host.setScopeId(QString::number(row.dwRemoteScopeId));
        if (host != QHostAddress::AnyIPv6) sample.remoteAddress = host.toString();
        sample.state = state(row.dwState);
        out.append(sample);
    });
    readTable<MIB_UDPTABLE_OWNER_PID>([](void *p, DWORD *n) {
        return GetExtendedUdpTable(p, n, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    }, [&](const MIB_UDPROW_OWNER_PID &row) {
        NetPortSample sample;
        sample.protocol = QStringLiteral("udp");
        sample.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
        out.append(sample);
    });
    readTable<MIB_UDP6TABLE_OWNER_PID>([](void *p, DWORD *n) {
        return GetExtendedUdpTable(p, n, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    }, [&](const MIB_UDP6ROW_OWNER_PID &row) {
        NetPortSample sample;
        sample.protocol = QStringLiteral("udp");
        sample.localPort = ntohs(static_cast<u_short>(row.dwLocalPort));
        out.append(sample);
    });
    return out;
}
}
