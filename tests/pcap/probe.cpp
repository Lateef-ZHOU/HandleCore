#include <PcapHandle.h>
#include "FakePcap.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
#include <cstdlib>
#include <thread>

void require(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
template <typename F> void until(F condition, bool pump = true)
{
    QElapsedTimer timer; timer.start();
    while (!condition())
    {
        require(timer.elapsed() < 4000, "timed out waiting for pcap test barrier");
        if (pump) QCoreApplication::processEvents();
        QThread::msleep(1);
    }
}
PcapParams params(bool seeded = false)
{
    bool ok = false;
    PcapParams p;
    p.host = {"test", "test", "test0", MAC::fromString("02:00:00:00:00:01", ok)};
    p.hostIPv4 = IPv4::fromString("192.0.2.1", ok);
    p.deviceIPv4 = IPv4::fromString("192.0.2.2", ok);
    p.hostPort = 1234; p.devicePort = 5678; p.readTimeout = std::chrono::milliseconds(20);
    p.kernelBufferBytes = 4 << 20;
    if (seeded) p.deviceMAC = MAC::fromString("02:00:00:00:00:02", ok);
    return p;
}
MAC peer() { bool ok = false; return MAC::fromString("02:00:00:00:00:02", ok); }
void open(PcapHandle &h, const PcapParams &p)
{
    require(h.start(p), "start rejected"); until([&] { return h.isOpen(); }, false);
}
void queuedArpAndUdp()
{
    FakePcap::reset();
    std::atomic<int> received{0};
    std::atomic<bool> valid{false};
    auto *owner = QThread::currentThread();
    PcapHandle h([&](const QByteArray &data, RxTime at) {
        valid = data == "inbound" && at == std::chrono::system_clock::from_time_t(1234567) +
            std::chrono::microseconds(654321) && QThread::currentThread() != owner;
        ++received;
    });
    const auto p = params(); open(h, p);
    auto s = FakePcap::last();
    require(s->snaplen == 65536 && s->promisc == 1 && s->timeout == 20 && s->buffer == (4 << 20)
            && s->name == "test0" && !s->filter.isEmpty(), "connection configuration changed");
    require(!h.start(p), "duplicate start accepted");
    require(h.send("first") && h.send("latest"), "ARP probe not accepted");
    require(FakePcap::sent().size() == 2, "ARP probe count changed");
    const ARPRequest request(p.host.macAddress, p.hostIPv4, p.deviceIPv4);
    FakePcap::inject(s, ARPReply(request, peer()));
    until([&] { return s->delivered == 1; }, false);
    require(FakePcap::sent().size() == 2, "worker sent pending UDP instead of queuing");
    until([&] { return FakePcap::sent().size() == 3; });
    require(UDPDatagram(FakePcap::sent().back().bytes).bodyView() == "latest", "ARP flush lost latest payload");
    require(h.send("known"), "known peer UDP rejected");
    require(kindOf(FakePcap::sent().back().bytes) == FrameKind::UDPDatagram, "learned MAC was not used");
    const ARPRequest incoming(peer(), p.deviceIPv4, p.hostIPv4);
    FakePcap::inject(s, incoming);
    until([&] { return s->delivered == 2; }, false);
    require(FakePcap::sent().size() == 4, "worker directly sent ARP reply");
    until([&] { return FakePcap::sent().size() == 5; });
    require(FakePcap::sent().back().bytes == ARPReply(incoming, p.host.macAddress), "queued ARP bytes corrupted");
    const UDPDatagram udp(p.host.macAddress, peer(), p.deviceIPv4, p.hostIPv4, p.devicePort, p.hostPort, "inbound");
    FakePcap::inject(s, udp); FakePcap::inject(s, udp, true); FakePcap::inject(s, "tiny");
    until([&] { return s->delivered == 5; });
    require(received == 1 && valid, "UDP/RxTime delivery or truncated frame policy changed");
    FakePcap::failSend = true;
    require(!h.send("failure"), "send failure not returned");
    for (const auto &sent : FakePcap::sent()) require(sent.thread == owner, "send ran outside owner thread");
    h.stop(); h.stop(); require(s->closed && !h.send("offline"), "stop did not release connection");
}
void staleReplies()
{
    for (bool reconnect : {false, true})
    {
        FakePcap::reset();
        PcapHandle h([](const QByteArray &, RxTime) {});
        const auto p = params(); open(h, p);
        auto old = FakePcap::last();
        FakePcap::inject(old, ARPRequest(peer(), p.deviceIPv4, p.hostIPv4));
        until([&] { return old->delivered == 1; }, false);
        if (reconnect)
        {
            FakePcap::fail(old);
            until([&] { return FakePcap::opens() == 2 && h.isOpen(); }, false);
        }
        else { h.stop(); open(h, p); }
        require(old->closed, "queued reply kept old connection alive");
        QCoreApplication::processEvents();
        require(FakePcap::sent().empty(), "old reply reached a closed or replacement connection");
        require(h.send("fresh") && kindOf(FakePcap::sent().back().bytes) == FrameKind::ARPRequest,
                "replacement connection reused ARP state");
        h.stop();
    }
    FakePcap::reset();
    {
        PcapHandle h([](const QByteArray &, RxTime) {}); const auto p = params(); open(h, p);
        auto s = FakePcap::last(); FakePcap::inject(s, ARPRequest(peer(), p.deviceIPv4, p.hostIPv4));
        until([&] { return s->delivered == 1; }, false);
    }
    QCoreApplication::processEvents();
    require(FakePcap::sent().empty() && FakePcap::last()->closed, "destructor left a live queued response");
}
void openFailuresAndStop()
{
    for (int stage = 1; stage <= 8; ++stage)
    {
        FakePcap::reset(); FakePcap::failStage = stage;
        PcapHandle h([](const QByteArray &, RxTime) {});
        require(h.start(params()) && h.wait(2000) && !h.isOpen(), "first open failure retried or stayed online");
        require(FakePcap::opens() == (stage == 1 ? 0u : 1u), "first failure opened multiple handles");
        if (stage > 1) require(FakePcap::last()->closed, "partly opened handle leaked");
        require(FakePcap::freeCodes == (stage == 8 ? 1 : 0), "filter ownership leak");
    }
    FakePcap::reset(); FakePcap::failStage = 9; // Positive activation warnings remain successful.
    QSemaphore entered, release;
    PcapHandle h([&](const QByteArray &, RxTime) { entered.release(); release.acquire(); });
    const auto p = params(true); open(h, p);
    require(h.send("seed") && kindOf(FakePcap::sent().back().bytes) == FrameKind::UDPDatagram, "MAC seed ignored");
    auto s = FakePcap::last();
    FakePcap::inject(s, UDPDatagram(p.host.macAddress, peer(), p.deviceIPv4, p.hostIPv4, p.devicePort, p.hostPort, "wait"));
    require(entered.tryAcquire(1, 2000), "receive callback did not enter");
    std::jthread releaser([&] { QThread::msleep(40); require(!s->closed, "connection closed during callback"); release.release(); });
    h.stop(); require(s->closed && s->delivered == 1, "stop returned before callback finished");
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    queuedArpAndUdp(); staleReplies(); openFailuresAndStop();
    std::puts("Pcap production I/O, owner-thread sends, stale replies, ARP/UDP and lifecycle passed");
}
