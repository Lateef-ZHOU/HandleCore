#include "FakeSerialPort.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>

struct sp_port { bool opened = false; };
namespace
{
struct State
{
    std::deque<QByteArray> incoming;
    std::vector<QByteArray> written;
    bool readFailure = false, openFailure = false, configFailure = false, writesBlocked = false;
    int openCount = 0, closeCount = 0, alive = 0, readCount = 0;
    int writeCount = 0, activeWrites = 0, maxWrites = 0, writeResult = -1000, baud = 0;
    sp_flowcontrol flow = SP_FLOWCONTROL_NONE;
    unsigned timeout = 0;
} state;
std::mutex gate;
std::condition_variable changed;
}

namespace FakeSerialPort
{
void reset() { const std::lock_guard lock(gate); state = {}; }
void enqueue(QByteArray bytes) { const std::lock_guard lock(gate); state.incoming.push_back(std::move(bytes)); changed.notify_all(); }
void failRead() { const std::lock_guard lock(gate); state.readFailure = true; changed.notify_all(); }
void failOpen(bool fail) { const std::lock_guard lock(gate); state.openFailure = fail; }
void failConfiguration(bool fail) { const std::lock_guard lock(gate); state.configFailure = fail; }
void writeResult(int result) { const std::lock_guard lock(gate); state.writeResult = result; }
void blockWrites(bool blocked) { const std::lock_guard lock(gate); state.writesBlocked = blocked; changed.notify_all(); }
int opens() { const std::lock_guard lock(gate); return state.openCount; }
int closes() { const std::lock_guard lock(gate); return state.closeCount; }
int livePorts() { const std::lock_guard lock(gate); return state.alive; }
int reads() { const std::lock_guard lock(gate); return state.readCount; }
int writesStarted() { const std::lock_guard lock(gate); return state.writeCount; }
int maxConcurrentWrites() { const std::lock_guard lock(gate); return state.maxWrites; }
int baudRate() { const std::lock_guard lock(gate); return state.baud; }
sp_flowcontrol flowControl() { const std::lock_guard lock(gate); return state.flow; }
unsigned writeTimeout() { const std::lock_guard lock(gate); return state.timeout; }
std::vector<QByteArray> writes() { const std::lock_guard lock(gate); return state.written; }
}

extern "C"
{
sp_return sp_get_port_by_name(const char *, sp_port **port)
{
    const std::lock_guard lock(gate);
    *port = new sp_port;
    ++state.alive;
    return SP_OK;
}
void sp_free_port(sp_port *port) { const std::lock_guard lock(gate); delete port; --state.alive; }
sp_return sp_open(sp_port *port, sp_mode)
{
    const std::lock_guard lock(gate);
    ++state.openCount;
    if (state.openFailure) return SP_ERR_FAIL;
    port->opened = true;
    return SP_OK;
}
sp_return sp_close(sp_port *port)
{
    const std::lock_guard lock(gate);
    if (!port->opened) return SP_ERR_ARG;
    port->opened = false;
    ++state.closeCount;
    return SP_OK;
}
sp_return sp_set_baudrate(sp_port *, int baud)
{
    const std::lock_guard lock(gate);
    state.baud = baud;
    return state.configFailure ? SP_ERR_SUPP : SP_OK;
}
sp_return sp_set_bits(sp_port *, int) { return SP_OK; }
sp_return sp_set_stopbits(sp_port *, int) { return SP_OK; }
sp_return sp_set_parity(sp_port *, sp_parity) { return SP_OK; }
sp_return sp_set_flowcontrol(sp_port *, sp_flowcontrol flow)
{
    const std::lock_guard lock(gate);
    state.flow = flow;
    return SP_OK;
}
sp_return sp_blocking_read_next(sp_port *, void *buf, size_t count, unsigned timeout)
{
    std::unique_lock lock(gate);
    ++state.readCount;
    changed.wait_for(lock, std::chrono::milliseconds(timeout), [] { return state.readFailure || !state.incoming.empty(); });
    if (state.readFailure) { state.readFailure = false; return SP_ERR_FAIL; }
    if (state.incoming.empty()) return SP_OK;
    auto &chunk = state.incoming.front();
    const auto n = std::min(count, static_cast<size_t>(chunk.size()));
    std::memcpy(buf, chunk.constData(), n);
    chunk.remove(0, static_cast<qsizetype>(n));
    if (chunk.isEmpty()) state.incoming.pop_front();
    return static_cast<sp_return>(n);
}
sp_return sp_blocking_write(sp_port *, const void *buf, size_t count, unsigned timeout)
{
    std::unique_lock lock(gate);
    ++state.writeCount;
    ++state.activeWrites;
    state.maxWrites = std::max(state.maxWrites, state.activeWrites);
    state.timeout = timeout;
    changed.wait(lock, [] { return !state.writesBlocked; });
    const int result = state.writeResult == -1000 ? static_cast<int>(count) : state.writeResult;
    if (result > 0) state.written.emplace_back(static_cast<const char *>(buf), result);
    --state.activeWrites;
    return static_cast<sp_return>(result);
}
}
