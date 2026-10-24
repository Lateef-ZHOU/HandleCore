#include "FakePcap.h"
#include <cstring>

struct pcap { std::shared_ptr<FakePcap::Session> session; };
extern "C" {
pcap_t *pcap_create(const char *name, char *error)
{
    if (FakePcap::failStage == 1) { std::memcpy(error, "create failure", sizeof("create failure")); return nullptr; }
    auto s = std::make_shared<FakePcap::Session>();
    const std::lock_guard lock(FakePcap::auditGate);
    s->id = static_cast<int>(FakePcap::sessions.size()); s->name = QString::fromLocal8Bit(name);
    FakePcap::sessions.push_back(s);
    return new pcap{s};
}
int pcap_set_snaplen(pcap_t *p, int v) { p->session->snaplen = v; return FakePcap::failStage == 2 ? -1 : 0; }
int pcap_set_promisc(pcap_t *p, int v) { p->session->promisc = v; return FakePcap::failStage == 3 ? -1 : 0; }
int pcap_set_timeout(pcap_t *p, int v) { p->session->timeout = v; return FakePcap::failStage == 4 ? -1 : 0; }
int pcap_set_buffer_size(pcap_t *p, int v) { p->session->buffer = v; return FakePcap::failStage == 5 ? -1 : 0; }
int pcap_activate(pcap_t *) { return FakePcap::failStage == 6 ? -1 : (FakePcap::failStage == 9 ? 1 : 0); }
int pcap_compile(pcap_t *p, bpf_program *program, const char *filter, int, bpf_u_int32)
{
    p->session->filter = QString::fromLatin1(filter);
    if (FakePcap::failStage == 7) return -1;
    program->bf_len = 1; program->bf_insns = new bpf_insn[1]{}; return 0;
}
int pcap_setfilter(pcap_t *, bpf_program *) { return FakePcap::failStage == 8 ? -1 : 0; }
void pcap_freecode(bpf_program *p) { delete[] p->bf_insns; ++FakePcap::freeCodes; }
void pcap_close(pcap_t *p) { p->session->closed = true; delete p; }
char *pcap_geterr(pcap_t *) { static char error[] = "injected pcap failure"; return error; }
int pcap_loop(pcap_t *p, int, pcap_handler callback, u_char *context)
{
    auto s = p->session;
    std::unique_lock lock(s->gate);
    for (;;)
    {
        s->wake.wait(lock, [&] { return s->interrupted || s->failed || !s->packets.empty(); });
        if (s->interrupted) return PCAP_ERROR_BREAK;
        if (s->failed) return PCAP_ERROR;
        auto packet = std::move(s->packets.front()); s->packets.pop_front();
        lock.unlock();
        pcap_pkthdr header{};
        header.caplen = static_cast<bpf_u_int32>(packet.bytes.size());
        header.len = header.caplen + (packet.truncated ? 1 : 0);
        header.ts.tv_sec = 1234567; header.ts.tv_usec = 654321;
        callback(context, &header, reinterpret_cast<const u_char *>(packet.bytes.constData()));
        packet.bytes.fill('\xCC'); // Emulate reuse of libpcap's callback buffer.
        ++s->delivered;
        lock.lock();
    }
}
void pcap_breakloop(pcap_t *p)
{
    const std::lock_guard lock(p->session->gate);
    p->session->interrupted = true; p->session->wake.notify_one();
}
int pcap_sendpacket(pcap_t *p, const u_char *data, int size)
{
    const std::lock_guard lock(FakePcap::auditGate);
    FakePcap::sends.push_back({p->session->id, QThread::currentThread(),
                             QByteArray(reinterpret_cast<const char *>(data), size)});
    return FakePcap::failSend ? -1 : 0;
}
}
