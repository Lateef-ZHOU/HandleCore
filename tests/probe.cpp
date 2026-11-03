#include <functional>
#include <HandleBase.h>

#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaEnum>

#if defined(QT_GUI_LIB) || defined(QT_QML_LIB) || defined(QT_QUICK_LIB) || defined(QT_TESTLIB_LIB)
#error The Handle core probe must depend on Qt Core only
#endif

static_assert(std::same_as<RxTime, std::chrono::system_clock::time_point>);

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
    bool retainConnection = false;
    std::shared_ptr<void> retainedConnection;
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
    CoreHandle(std::shared_ptr<State> state, std::function<void(const QByteArray &, RxTime)> callback)
        : state_(std::move(state)), callback_(std::move(callback)) {}
    ~CoreHandle() override { close(); }
Q_SIGNALS:
    void connectionChanged(HandleCore::ResourcePhase) override;
    void logMessage(const QString &) const override;
private:
    // 测试探针借此注入阻塞/断言；生产 Handle 不保存上传回调。
    void onData(const QByteArray &data, RxTime at) override { callback_(data, at); }
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
    bool connLoop(std::shared_ptr<Connection> conn,
        std::function<void(std::shared_ptr<Connection>, const QByteArray &, RxTime)> sink) override
    {
        if (state_->retainConnection) state_->retainedConnection = conn;
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
        require(QThread::currentThread() == thread(), "connSend not on owner thread");
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
            return {QByteArray("response"), RXRuling::Reject, {}};
        if (raw == "borrowed")
            return {QByteArray::fromRawData(raw.constData(), raw.size()), RXRuling::Reject, {}};
        if (raw == "drop")
            return {};
        return {raw, RXRuling::Decode, at};
    }
    TXRuling onTX(const QByteArray &raw, std::shared_ptr<Connection>) const override
    {
        if (raw == "drop")
            return {};
        return {QByteArray("tx:") + raw, TXRuling::Encode};
    }
    const std::shared_ptr<State> state_;
    const std::function<void(const QByteArray &, RxTime)> callback_;
};

static_assert(HandleLike<CoreHandle>);

void dispatchAndSignals()
{
    auto state = std::make_shared<State>();
    state->input = {"data", "reply", "borrowed", "drop"};
    const auto *ownerThread = QThread::currentThread();
    QObject receiver;
    int connected = 0, disconnected = 0, logs = 0;
    CoreHandle handle(state, [state, ownerThread](const QByteArray &data, RxTime at) {
        require(QThread::currentThread() != ownerThread, "callback not on receive thread");
        require(data == "data" && at == state->at, "payload or RxTime changed");
        ++state->callbacks;
    });
    QObject::connect(&handle, &CoreHandle::connectionChanged, &receiver, [&](HandleCore::ResourcePhase phase) {
        require(QThread::currentThread() == ownerThread, "signal receiver not on owner thread");
        if (phase == HandleCore::ResourcePhase::Acquired) ++connected;
        if (phase == HandleCore::ResourcePhase::Released) ++disconnected;
    });
    QObject::connect(&handle, &CoreHandle::logMessage, &receiver, [&](const QString &) {
        require(QThread::currentThread() == ownerThread, "log receiver not on owner thread");
        ++logs;
    });
    handle.close();
    require(!handle.send("offline"), "disconnected send accepted");
    require(handle.open(Params{state}), "start rejected");
    acquire(state->processed);
    require(state->sent.empty(), "reply sent before owner event delivery");
    state->input[2].fill('x'); // 模拟底层复用已经上交的借用缓冲。
    QCoreApplication::sendPostedEvents(&handle, QEvent::MetaCall);
    require(handle.isOpen() && !handle.open(Params{state}), "duplicate start accepted");
    require(handle.send("command") && !handle.send("drop"), "TX ruling changed");
    state->sendSucceeds = false; // 收包已停在 leaveLoop，只由当前线程发送。
    require(!handle.send("failure"), "send failure swallowed");
    handle.close();
    handle.close();
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    require(connected == 1 && disconnected == 1 && logs >= 2, "Qt Core signal delivery failed");
    require(state->callbacks == 1 && state->closes == 1 && !handle.isRunning() && !handle.isOpen(),
            "dispatch or resource release failed");
    const std::vector<QByteArray> expected{"response", "borrowed", "tx:command", "tx:failure"};
    require(state->sent == expected, "RX/TX actions produced wrong output");
}

void commandLists()
{
    auto state = std::make_shared<State>();
    CoreHandle handle(state, [](const QByteArray &, RxTime) {});
    require(handle.send(QList<QByteArray>{}) && state->sent.empty(), "empty command list sent data");
    require(handle.open({state}), "command list open rejected");
    acquire(state->processed);
    require(handle.send(QList<QByteArray>{"one", "two"}), "command list failed");
    require(state->sent == std::vector<QByteArray>{"tx:one", "tx:two"}, "command order changed");
    require(!handle.send(QList<QByteArray>{"three", "drop", "skipped"}), "TX drop ignored");
    require(state->sent == std::vector<QByteArray>{"tx:one", "tx:two", "tx:three"}, "list continued after TX drop");
    state->sendSucceeds = false;
    require(!handle.send(QList<QByteArray>{"failure", "skipped"}), "transport failure ignored");
    require(state->sent == std::vector<QByteArray>{"tx:one", "tx:two", "tx:three", "tx:failure"}, "list continued after transport failure");
    handle.close();
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
    require(handle.open(Params{state}), "callback probe start rejected");
    acquire(state->callbackEntered);
    std::jthread release([state] {
        acquire(state->breaking);
        require(state->callbackActive, "callback unexpectedly exited before stop");
        state->allowCallback.release();
    });
    handle.close();
    require(!state->callbackActive && state->callbacks == 1 && state->closes == 1,
            "stop returned before callback/resource completion");
}

void stopDuringOpen()
{
    auto state = std::make_shared<State>();
    state->delayOpen = true;
    CoreHandle handle(state, [](const QByteArray &, RxTime) { qFatal("unexpected callback"); });
    require(handle.open(Params{state}), "open probe start rejected");
    acquire(state->opening);
    std::jthread release([state] {
        acquire(state->breaking);
        state->allowOpen.release();
    });
    handle.close();
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
                     [&](HandleCore::ResourcePhase phase) { if (phase != HandleCore::ResourcePhase::AboutToAcquire) ++failureNotifications; });
    require(failure.open(Params{failed}) && failure.wait(5000), "initial failure did not finish");
    failure.close();
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    require(failed->opens == 1 && failed->loops == 0 && failureNotifications == 0,
            "initial failure semantics changed");

    auto state = std::make_shared<State>();
    state->reconnect = true;
    CoreHandle handle(state, [](const QByteArray &, RxTime) {});
    int connected = 0, disconnected = 0;
    QObject::connect(&handle, &CoreHandle::connectionChanged, &receiver,
                     [&](HandleCore::ResourcePhase phase) { if (phase == HandleCore::ResourcePhase::Acquired) ++connected;
        if (phase == HandleCore::ResourcePhase::Released) ++disconnected; });
    require(handle.open(Params{state}), "reconnect probe start rejected");
    acquire(state->entered);
    require(state->opens == 2 && state->closes == 1 && handle.isOpen(), "reconnect failed");
    handle.close();
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    require(connected == 2 && disconnected == 2 && state->closes == 2,
            "reconnect notifications or cleanup changed");
}

void synchronousDisconnect()
{
    auto state = std::make_shared<State>();
    CoreHandle handle(state, [](const QByteArray &, RxTime) {});
    QObject receiver;
    int disconnected = 0;
    QObject::connect(&handle, &CoreHandle::connectionChanged, &receiver, [&](HandleCore::ResourcePhase phase) {
        if (phase != HandleCore::ResourcePhase::AboutToRelease) return;
        require(QThread::currentThread() == handle.thread(), "disconnect was queued from worker");
        require(state->breaking.available() == 0 && handle.isOpen(), "disconnect arrived after break/release");
        require(handle.send("off"), "disconnect receiver cannot send final command");
        ++disconnected;
    });
    require(handle.open({state}), "disconnect probe start rejected");
    acquire(state->entered);
    handle.close();
    require(disconnected == 1 && !handle.isOpen(), "stop must notify synchronously then release");
    acquire(state->breaking);
    require(handle.open({state}), "immediate reopen rejected");
    acquire(state->entered);
    QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
    require(disconnected == 1 && handle.isOpen(), "old disconnect reached new connection");
    handle.close();
    handle.close();
    require(disconnected == 2 && state->sent == std::vector<QByteArray>{"tx:off", "tx:off"},
            "repeated stop duplicated final command");
}

void releasePhasesAndRetainedConnection()
{
    using Phase = HandleCore::ResourcePhase;
    auto state = std::make_shared<State>();
    state->retainConnection = true;
    CoreHandle handle(state, [](const QByteArray &, RxTime) {});
    std::mutex gate;
    std::vector<Phase> phases;
    QObject::connect(&handle, &CoreHandle::connectionChanged, &handle, [&](Phase phase) {
        const std::lock_guard guard(gate);
        phases.push_back(phase);
        if (phase == Phase::Acquired)
            require(handle.isOpen(), "Acquired must follow connection publication");
        if (phase == Phase::AboutToRelease) {
            require(QThread::currentThread() == handle.thread(), "pre-release must be synchronous on owner");
            require(handle.send("final"), "pre-release cannot send on the still live connection");
        }
        if (phase == Phase::Released)
            require(!handle.isOpen(), "Released must revoke publication even while another share exists");
    }, Qt::DirectConnection);
    require(handle.open({state}), "phase probe rejected start");
    acquire(state->entered);
    handle.close();
    handle.close();
    require(phases == std::vector<Phase>{Phase::AboutToAcquire, Phase::Acquired,
        Phase::AboutToRelease, Phase::Released}, "resource phase order or stop idempotence changed");
    require(state->closes == 0 && !handle.isOpen() && !handle.send("late"), "retained old resource admitted a send");
    state->retainedConnection.reset();
    require(state->closes == 1, "retained resource leaked");
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto phase = QMetaEnum::fromType<HandleCore::ResourcePhase>();
    require(phase.isValid() && phase.keyCount() == 4
            && QByteArray(phase.scope()) == "HandleCore"
            && phase.keyToValue("AboutToAcquire") == 0
            && phase.keyToValue("Acquired") == 1
            && phase.keyToValue("AboutToRelease") == 2
            && phase.keyToValue("Released") == 3,
            "resource phase namespace metadata changed");
    dispatchAndSignals();
    commandLists();
    stopDuringCallback();
    stopDuringOpen();
    reconnectAndInitialFailure();
    synchronousDisconnect();
    releasePhasesAndRetainedConnection();
    std::puts("PASS: Qt Core-only Handle contracts, dispatch, signals, stop and reconnect");
}

#include "probe.moc"
