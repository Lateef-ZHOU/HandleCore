#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <QByteArray>
#include <QString>
#include <QThread>
#include <pcap.h>

namespace FakePcap {
struct Packet { QByteArray bytes; bool truncated = false; };
struct Session {
    std::mutex gate;
    std::condition_variable wake;
    std::deque<Packet> packets;
    bool interrupted = false;
    bool failed = false;
    std::atomic<int> delivered{0};
    std::atomic<bool> closed{false};
    int id = 0;
    int snaplen = 0, promisc = 0, timeout = 0, buffer = 0;
    QString name, filter;
};
struct Send { int id; QThread *thread; QByteArray bytes; };
inline std::mutex auditGate;
inline std::vector<std::shared_ptr<Session>> sessions;
inline std::vector<Send> sends;
inline std::atomic<int> failStage{0}, freeCodes{0};
inline std::atomic<bool> failSend{false};
inline void reset()
{
    const std::lock_guard lock(auditGate);
    sessions.clear(); sends.clear(); failStage = 0; freeCodes = 0; failSend = false;
}
inline std::shared_ptr<Session> last()
{
    const std::lock_guard lock(auditGate);
    return sessions.empty() ? nullptr : sessions.back();
}
inline size_t opens() { const std::lock_guard lock(auditGate); return sessions.size(); }
inline std::vector<Send> sent() { const std::lock_guard lock(auditGate); return sends; }
inline void inject(const std::shared_ptr<Session> &s, QByteArray bytes, bool truncated = false)
{
    const std::lock_guard lock(s->gate);
    s->packets.push_back({std::move(bytes), truncated}); s->wake.notify_one();
}
inline void fail(const std::shared_ptr<Session> &s)
{
    const std::lock_guard lock(s->gate); s->failed = true; s->wake.notify_one();
}
}
