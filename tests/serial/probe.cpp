#include <SerialHandle.h>
#include "FakeSerialPort.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <QCoreApplication>
#include <QSemaphore>

namespace
{
void require(bool ok, const char *what) { if (!ok) qFatal("%s", what); }
template <typename Predicate> bool until(Predicate ready, int ms = 3000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!ready() && std::chrono::steady_clock::now() < end) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return ready();
}
class Lines
{
public:
    void feed(const QByteArray &chunk, RxTime at, const DataCallback &deliver)
    {
        tail_.append(chunk);
        for (auto newline = tail_.indexOf('\n'); newline >= 0; newline = tail_.indexOf('\n'))
        {
            const QByteArray line = tail_.first(newline);
            tail_.remove(0, newline + 1);
            deliver(line, at);
        }
    }
private:
    QByteArray tail_;
};
}

class LineHandle final : public SerialHandle<Lines>
{
    Q_OBJECT
public:
    using SerialHandle::SerialHandle;
    ~LineHandle() override { stop(); }
Q_SIGNALS:
    void connectionChanged(bool) override;
    void logMessage(const QString &) const override;
private:
    RXRuling onRX(const QByteArray &frame, RxTime at, std::shared_ptr<ConnType>) override
    {
        if (frame == "reject") return {RXRuling::Reject, "ack", {}};
        if (frame == "drop") return {};
        return {RXRuling::Decode, frame, at};
    }
    TXRuling onTX(const QByteArray &payload, std::shared_ptr<ConnType>) const override
    {
        return payload == "drop" ? TXRuling{} : TXRuling{TXRuling::Encode, payload};
    }
};

class MissingRX : public SerialHandle<Lines>
{
    void connectionChanged(bool) override {}
    void logMessage(const QString &) const override {}
    TXRuling onTX(const QByteArray &, std::shared_ptr<ConnType>) const override { return {}; }
};
class MissingTX : public SerialHandle<Lines>
{
    void connectionChanged(bool) override {}
    void logMessage(const QString &) const override {}
    RXRuling onRX(const QByteArray &, RxTime, std::shared_ptr<ConnType>) override { return {}; }
};
static_assert(std::is_abstract_v<SerialHandle<Lines>> && std::is_abstract_v<MissingRX> && std::is_abstract_v<MissingTX>);
static_assert(HandleLike<LineHandle>);

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace FakeSerialPort;
    SerialParams params{{"controlled", {}}, 230400, DataBits::D8, StopBits::One, SP_PARITY_NONE, SP_FLOWCONTROL_RTSCTS, 1200};
    reset();
    std::vector<QByteArray> received;
    std::mutex receivedGate;
    std::atomic<bool> onWorker{true}, typedTime{true};
    int connections = 0;
    LineHandle handle([&](const QByteArray &frame, RxTime at) {
        onWorker = onWorker && QThread::currentThread() != app.thread();
        typedTime = typedTime && at != RxTime{};
        const std::lock_guard lock(receivedGate); received.push_back(frame);
    });
    const auto receivedCount = [&] { const std::lock_guard lock(receivedGate); return received.size(); };
    QStringList logs;
    QObject::connect(&handle, &LineHandle::connectionChanged, &app, [&](bool connected) { connections += connected ? 1 : 10; });
    QObject::connect(&handle, &LineHandle::logMessage, &app, [&](const QString &text) { logs.push_back(text); });
    require(handle.start(params) && until([&] { return handle.isOpen(); }), "open failed");
    require(!handle.start(params), "duplicate start accepted");
    require(baudRate() == 230400 && flowControl() == SP_FLOWCONTROL_RTSCTS, "explicit serial config lost");
    enqueue("one\ntwo\npar");
    require(until([&] { return receivedCount() == 2; }), "burst frames wait for next read");
    enqueue("tial\ndrop\n");
    require(until([&] { return receivedCount() == 3; }), "split frame lost");
    require(handle.send("command") && writeTimeout() == 1200, "send or timeout failed");
    require(!handle.send("drop"), "TX Drop accepted");
    writeResult(2);
    require(!handle.send("long"), "short write accepted");
    writeResult(SP_ERR_FAIL);
    require(!handle.send("long"), "write failure accepted");
    writeResult(-1000);

    // 阻塞收包线程的应答写，再让所属线程发命令；同时仍能进入读取。
    blockWrites(true);
    const int before = writesStarted();
    enqueue("reject\n");
    require(until([&] { return writesStarted() == before + 1; }), "Reject did not write");
    std::thread release([] { QThread::msleep(50); FakeSerialPort::blockWrites(false); });
    require(handle.send("parallel"), "concurrent command failed");
    release.join();
    require(maxConcurrentWrites() == 1, "two writes overlapped");

    enqueue("old-tail");
    const int readBefore = reads();
    require(until([&] { return reads() > readBefore; }), "tail not read");
    failRead();
    require(until([&] { return opens() == 2 && handle.isOpen(); }), "I/O failure did not reconnect");
    enqueue("fresh\n");
    require(until([&] { return receivedCount() == 4; }), "new connection did not receive");
    handle.stop(); handle.stop();
    QCoreApplication::processEvents();
    require(received == std::vector<QByteArray>{"one", "two", "partial", "fresh"}, "framer state leaked across connections");
    require(connections == 11 && livePorts() == 0 && closes() == 2, "connection notifications or resource release changed");
    require(onWorker && typedTime, "callback thread/time changed");
    require(logs.contains("串口发送超时，仅写入 2/4 字节") && logs.contains("串口发送失败，错误码 -2"), "write diagnostics missing");

    reset(); failOpen(true);
    require(handle.start(params) && handle.wait(2000), "first-open failure did not exit");
    handle.stop();
    require(opens() == 1 && livePorts() == 0, "first-open failure retried or leaked");
    reset(); failConfiguration(true);
    require(handle.start(params) && handle.wait(2000), "config failure did not exit");
    handle.stop();
    require(closes() == 1 && livePorts() == 0, "partial open leaked");

    reset();
    QSemaphore entered, leave;
    std::atomic<bool> callbackDone{false};
    {
        LineHandle delayed([&](const QByteArray &, RxTime) { entered.release(); leave.acquire(); callbackDone = true; });
        require(delayed.start(params) && until([&] { return delayed.isOpen(); }), "delayed open failed");
        enqueue("wait\n"); require(entered.tryAcquire(1, 2000), "callback not entered");
        std::thread unblock([&] { QThread::msleep(30); leave.release(); });
        delayed.stop();
        require(callbackDone, "stop returned before callback exit");
        unblock.join();
    }
    require(livePorts() == 0, "destruction leaked port");
    std::puts("PASS serial transport, protocol polymorphism, burst, reconnect, writes and shutdown");
}
#include "probe.moc"
