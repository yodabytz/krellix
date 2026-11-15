#include <QCoreApplication>
#include <QThread>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto mode = qgetenv("KRELLPKG_TEST_CASE");
    if (mode == "timeout") { QThread::sleep(90); return 1; }
    if (mode == "malformed") { std::puts("invalid"); return 0; }
    if (mode == "oversize") { for(int i=0;i<1100000;++i) std::putchar('x'); return 0; }
    if (mode == "error") { std::puts("{\"status\":\"error\",\"detail\":\"Offline\"}"); return 0; }
    if (mode == "empty") { std::puts("{\"status\":\"ok\",\"packages\":[]}"); return 0; }
    std::puts("{\"status\":\"ok\",\"packages\":[{\"id\":\"Vendor.One\",\"name\":\"<b>One</b>\",\"installed\":\"1\",\"available\":\"2\"},{\"id\":\"Vendor.Two\",\"name\":\"Two\",\"installed\":\"1\",\"available\":\"2\"}]}");
}
