#include <HandleBase.h>

#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include <QCoreApplication>
#include <QEvent>

#if defined(QT_GUI_LIB) || defined(QT_QML_LIB) || defined(QT_QUICK_LIB) || defined(QT_TESTLIB_LIB)
#error The Handle core probe must depend on Qt Core only
#endif

static_assert(std::same_as<RxTime, std::chrono::system_clock::time_point>);

// 只可赋值、不允许复制构造；比较可用于条件但不隐式转换为 bool。
// 防止 concept 施加基类并不需要的 copyable/equality_comparable 要求。
struct Condition { explicit operator bool() const { return false; } };
struct AssignOnlyParams
{
    AssignOnlyParams() = default;
    AssignOnlyParams(const AssignOnlyParams &) = delete;
    void operator=(const AssignOnlyParams &) {}
    Condition operator!=(const AssignOnlyParams &) const { return {}; }
};
static_assert(HandleParameters<AssignOnlyParams>);

void require(bool value, const char *message)
{
    if (!value)
        qFatal("%s", message);
}

void acquire(QSemaphore &gate)
{
    require(gate.tryAcquire(1, 5000), "probe barrier timed out");
}

struct State
{
    QSemaphore opening, allowOpen, entered, processed, leaveLoop, breaking;
    QSemaphore callbackEntered, allowCallback;
    std::atomic<int> opens{0}, closes{0}, loops{0}, callbacks{0};
    std::atomic<bool> callbackActive{false};
    bool firstOpenFails = false;
    bool delayOpen = false;
    bool reconnect = false;
    bool sendSucceeds = true;
    std::vector<QByteArray> input;
    const RxTime at = RxTime{} + std::chrono::microseconds(1234567);
    std::mutex sentGate;
    std::vector<QByteArray> sent;
};

struct Params
{
    std::shared_ptr<State> state;
    bool operator==(const Params &) const = default;
};

struct Connection
{
    std::shared_ptr<State> state;
    ~Connection() { ++state->closes; }
};

class CoreHandle final : public Handle<Params, Connection>
{
    Q_OBJECT
public:
    CoreHandle(std::shared_ptr<State> state, DataCallback callback)
        : Handle(std::move(callback)), state_(std::move(state)) {}
    ~CoreHandle() override { stop(); }
Q_SIGNALS:
    void connectionChanged(bool) override;
    void logMessage(const QString &) const override;
private:
    std::shared_ptr<Connection> connOpen(const Params &params) override
    {
        require(params.state == state_, "reconnect parameters changed");
        ++state_->opens;
        state_->opening.release();
        if (state_->delayOpen)
            acquire(state_->allowOpen);
        if (state_->firstOpenFails)
            return {};
        return std::make_shared<Connection>(state_);
    }
    bool connLoop(std::shared_ptr<Connection> conn, Sink sink) override
    {
        ++state_->loops;
        if (state_->reconnect && state_->opens == 1)
            return false;
        state_->entered.release();
        for (const auto &data : state_->input)
            sink(conn, data, state_->at);
        state_->processed.release();
        acquire(state_->leaveLoop);
        return true;
    }
    void connBreak(std::shared_ptr<Connection>) const override
    {
        state_->breaking.release();
        state_->leaveLoop.release();
    }
    bool connSend(std::shared_ptr<Connection> conn, const QByteArray &bytes) const override
    {
        if (!conn)
        {
            Q_EMIT logMessage(QStringLiteral("probe disconnected"));
            return false;
        }
        const std::lock_guard guard(state_->sentGate);
        state_->sent.push_back(bytes);
        return state_->sendSucceeds;
    }
    RXRuling onRX(const QByteArray &raw, RxTime at, std::shared_ptr<Connection>) override
    {
        if (raw == "reply")
            return {RXRuling::Reject, QByteArray("response"), {}};
        if (raw == "drop")
            return {};
        return {RXRuling::Decode, raw, at};
    }
    TXRuling onTX(const QByteArray &raw, std::shared_ptr<Connection>) const override
    {
        if (raw == "drop")
            return {};
        return {TXRuling::Encode, QByteArray("tx:") + raw};
    }
    const std::shared_ptr<State> state_;
};

static_assert(HandleLike<CoreHandle>);

void dispatchAndSignals()
{
    auto state = std::make_shared<State>();
    state->input = {"data", "reply", "drop"};
    const auto *ownerThread = QThread::currentThread();
    QObject receiver;
    int connected = 0, disconnected = 0, logs = 0;
    CoreHandle handle(state, [state, ownerThread](const QByteArray &data, RxTime at) {
        require(QThread::currentThread() != ownerThread, "callback not on receive thread");
        require(data == "data" && at == state->at, "payload or RxTime changed");
        ++state->callbacks;
    });
    QObject::connect(&handle, &CoreHandle::connectionChanged, &receiver, [&](bool open) {
        require(QThread::currentThread() == ownerThread, "signal receiver not on owner thread");
        open ? ++connected : ++disconnected;
    });
    QObject::connect(&handle, &CoreHandle::logMessage, &receiver, [&](const QString &) {
        require(QThread::currentThread() == ownerThread, "log receiver not on owner thread");
        ++logs;
    });
    handle.stop();
    require(!handle.send("offline"), "disconnected send accepted");
    require(handle.start(Params{state}), "start rejected");
    acquire(state->processed);
    require(handle.isOpen() && !handle.start(Params{state}), "duplicate start accepted");
    require(handle.send("command") && !handle.send("drop"), "TX ruling changed");
    state->sendSucceeds = false; // 收包已停在 leaveLoop，只由当前线程发送。
    require(!handle.send("failure"), "send failure swallowed");
    handle.stop();
    handle.stop();
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    require(connected == 1 && disconnected == 1 && logs >= 2, "Qt Core signal delivery failed");
    require(state->callbacks == 1 && state->closes == 1 && !handle.isRunning() && !handle.isOpen(),
            "dispatch or resource release failed");
    const std::vector<QByteArray> expected{"response", "tx:command", "tx:failure"};
    require(state->sent == expected, "RX/TX actions produced wrong output");
}

void stopDuringCallback()
{
    auto state = std::make_shared<State>();
    state->input = {"data"};
    CoreHandle handle(state, [state](const QByteArray &, RxTime) {
        state->callbackActive = true;
        state->callbackEntered.release();
        acquire(state->allowCallback);
        state->callbackActive = false;
        ++state->callbacks;
    });
    require(handle.start(Params{state}), "callback probe start rejected");
    acquire(state->callbackEntered);
    std::jthread release([state] {
        acquire(state->breaking);
        require(state->callbackActive, "callback unexpectedly exited before stop");
        state->allowCallback.release();
    });
    handle.stop();
    require(!state->callbackActive && state->callbacks == 1 && state->closes == 1,
            "stop returned before callback/resource completion");
}

void stopDuringOpen()
{
    auto state = std::make_shared<State>();
    state->delayOpen = true;
    CoreHandle handle(state, [](const QByteArray &, RxTime) { qFatal("unexpected callback"); });
    require(handle.start(Params{state}), "open probe start rejected");
    acquire(state->opening);
    std::jthread release([state] {
        acquire(state->breaking);
        state->allowOpen.release();
    });
    handle.stop();
    require(state->loops == 0 && state->closes == 1 && !handle.isOpen(), "early stop lost or leaked");
}

void reconnectAndInitialFailure()
{
    auto failed = std::make_shared<State>();
    failed->firstOpenFails = true;
    CoreHandle failure(failed, [](const QByteArray &, RxTime) {});
    int failureNotifications = 0;
    QObject receiver;
    QObject::connect(&failure, &CoreHandle::connectionChanged, &receiver,
                     [&](bool) { ++failureNotifications; });
    require(failure.start(Params{failed}) && failure.wait(5000), "initial failure did not finish");
    failure.stop();
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    require(failed->opens == 1 && failed->loops == 0 && failureNotifications == 0,
            "initial failure semantics changed");

    auto state = std::make_shared<State>();
    state->reconnect = true;
    CoreHandle handle(state, [](const QByteArray &, RxTime) {});
    int connected = 0, disconnected = 0;
    QObject::connect(&handle, &CoreHandle::connectionChanged, &receiver,
                     [&](bool open) { open ? ++connected : ++disconnected; });
    require(handle.start(Params{state}), "reconnect probe start rejected");
    acquire(state->entered);
    require(state->opens == 2 && state->closes == 1 && handle.isOpen(), "reconnect failed");
    handle.stop();
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    require(connected == 1 && disconnected == 1 && state->closes == 2,
            "reconnect notifications or cleanup changed");
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    dispatchAndSignals();
    stopDuringCallback();
    stopDuringOpen();
    reconnectAndInitialFailure();
    std::puts("PASS: Qt Core-only Handle contracts, dispatch, signals, stop and reconnect");
}

#include "probe.moc"
