#include <SimCardHandle.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QtEndian>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
static void require(bool ok, const char *text)
{
    if (!ok) { std::cerr << text << '\n'; std::exit(1); }
}
template<class T> static T read(const QByteArray &data, qsizetype offset = 0)
{
    require(offset >= 0 && data.size() - offset >= static_cast<qsizetype>(sizeof(T)), "truncated test result");
    return qFromUnaligned<T>(data.cbegin() + offset);
}
template<class F> static bool until(F ready, int ms = 2500)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return ready();
}
struct Input
{
    int value = 0;
    bool empty = false;
    QSemaphore *entered = nullptr;
    QSemaphore *release = nullptr;
    Input() = default;
    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;
    Input(Input &&) = default;
    Input &operator=(Input &&) = default;
};
// No Q_DECLARE_METATYPE: a move-only application value must cross the queue unchanged.
class Generator
{
public:
    using Input = ::Input;
    static inline std::atomic<int> created{0}, destroyed{0};
    Generator() { ++created; }
    Generator(const Generator &) = delete;
    ~Generator() { require(QThread::currentThread() == thread_, "generator destroyed on wrong thread"); ++destroyed; }
    QByteArray synthesize(quint64 id, RxTime begin, RxTime end, const Input &input)
    {
        require(QThread::currentThread() == thread_ && end - begin == 1s, "generator thread/window changed");
        ++sequence_;
        if (input.entered) { input.entered->release(); input.release->acquire(); }
        if (input.empty) return {};
        QByteArray bytes(static_cast<qsizetype>(sizeof(id) + 2 * sizeof(int)), Qt::Uninitialized);
        qToUnaligned(id, bytes.data());
        qToUnaligned(sequence_, bytes.data() + sizeof(id));
        qToUnaligned(input.value, bytes.data() + sizeof(id) + sizeof(int));
        return bytes;
    }
private:
    QThread *const thread_ = QThread::currentThread();
    int sequence_ = 0;
};
struct NoInput {};
struct NoSynthesis { using Input = int; };
struct WrongReturn { using Input = int; int synthesize(quint64, RxTime, RxTime, const int &); };
struct NoDefault { using Input = int; NoDefault(int); QByteArray synthesize(quint64, RxTime, RxTime, const int &); };
struct NoMoveInput { using Input = Generator; QByteArray synthesize(quint64, RxTime, RxTime, const Input &); };
static_assert(SimCardGenerator<Generator>);
static_assert(!SimCardGenerator<NoInput> && !SimCardGenerator<NoSynthesis> && !SimCardGenerator<WrongReturn> &&
              !SimCardGenerator<NoDefault> && !SimCardGenerator<NoMoveInput>);
class ProbeHandle final : public SimCardHandle<Generator>
{
    Q_OBJECT
public:
    using SimCardHandle::SimCardHandle;
    ~ProbeHandle() override { stop(); }
Q_SIGNALS:
    void connectionChanged(bool connected) override;
    void logMessage(const QString &text) const final;
};
using Params = ProbeHandle::ParamType;
using Command = SimCardWire::Command;
static QByteArray command(Command::Kind kind, quint64 id, int secs = 1)
{ return SimCardWire::encode(Command{kind, secs, id}); }

static void windowsAndState()
{
    const auto owner = QThread::currentThread();
    std::mutex gate;
    QByteArray received;
    RxTime receivedAt{};
    std::atomic<int> frames{0};
    ProbeHandle handle([&](const QByteArray &data, RxTime at) {
        require(QThread::currentThread() != owner, "delivery ran on owner");
        { const std::lock_guard lock(gate); received = data; receivedAt = at; }
        ++frames;
    });
    int queries = 0, logs = 0;
    QObject::connect(&handle, &ProbeHandle::logMessage, &handle, [&](const QString &) { ++logs; });
    require(handle.start({}) && until([&] { return !handle.isRunning(); }), "missing capture should fail open");
    handle.stop();
    require(!handle.isOpen() && frames == 0, "missing capture created a working connection");
    require(!handle.send(command(Command::Stop, 1)), "offline send accepted");
    RxTime capturedBegin{}, capturedEnd{};
    Params params;
    params.sourceKey = "source-A";
    params.capture = [&](RxTime begin, RxTime end) -> std::optional<Input> {
        require(QThread::currentThread() == owner, "capture ran on worker");
        ++queries; capturedBegin = begin; capturedEnd = end;
        Input input; input.value = 42; return input;
    };
    require(handle.start(params) && until([&] { return handle.isOpen(); }), "open failed");
    for (const auto &bad : {QByteArray("short"), command(Command::Stop, 0) + 'x', command(static_cast<Command::Kind>(44), 0),
                           command(Command::Accumulate, 0, 0), command(Command::Accumulate, 0, -1),
                           command(Command::Accumulate, 0, std::numeric_limits<int>::max())})
        require(!handle.send(bad), "invalid command accepted");
    const auto sentAt = std::chrono::system_clock::now();
    require(handle.send(command(Command::Accumulate, 10)), "accumulation rejected");
    QThread::msleep(1150); // Owner is stalled: no early query, fixed window must not drift.
    require(queries == 0 && frames == 0, "timer or capture ran on worker");
    require(until([&] { return frames == 1; }), "missing first frame");
    {
        const std::lock_guard lock(gate);
        require(capturedBegin >= sentAt && capturedEnd - capturedBegin == 1s && receivedAt == capturedEnd &&
                capturedEnd < std::chrono::system_clock::now(), "fixed window/time changed");
        require(read<quint64>(received) == 10 && read<int>(received, 8) == 1 &&
                read<int>(received, 12) == 42, "request, state or moved input changed");
    }
    require(handle.send(command(Command::Accumulate, 11)) && handle.send(command(Command::Stop, 11)), "cancel pending");
    require(handle.send(command(Command::Accumulate, 12)) && handle.send(command(Command::Accumulate, 13)) &&
            handle.send(command(Command::Stop, 12)), "replace pending");
    require(until([&] { return frames == 2; }) && queries == 2, "old stop affected replacement or canceled timer survived");
    {
        const std::lock_guard lock(gate);
        require(read<quint64>(received) == 13 && read<int>(received, 8) == 2,
                "generator recreated between requests");
    }
    Params same = params;
    same.capture = [](RxTime, RxTime) -> std::optional<Input> { return {}; };
    require(!handle.start(same), "same source should not replace capture or reset connection");
    handle.stop();
    params.capture = [&](RxTime, RxTime) -> std::optional<Input> { ++queries; return {}; };
    require(handle.start(params) && until([&] { return handle.isOpen(); }), "reopen for missing input");
    QCoreApplication::processEvents();
    const int beforeLogs = logs;
    require(handle.send(command(Command::Accumulate, 14)) && until([&] { return queries == 3; }), "missing input not queried");
    require(frames == 2 && logs == beforeLogs, "missing input not silent");
    params.sourceKey = "source-B";
    params.capture = [&](RxTime, RxTime) -> std::optional<Input> { ++queries; Input input; input.empty = queries == 4; return input; };
    require(handle.start(params) && until([&] { return handle.isOpen(); }), "changed source failed to reconnect");
    require(handle.send(command(Command::Accumulate, 15)) && until([&] { return queries == 4; }), "empty result capture");
    require(handle.send(command(Command::Accumulate, 16)) && until([&] { return frames == 3; }), "next frame after empty result");
    {
        const std::lock_guard lock(gate);
        require(read<quint64>(received) == 16 && read<int>(received, 8) == 2,
                "reconnect did not reset state, or empty result reset generator");
    }
    require(handle.send(command(Command::Accumulate, 17)), "pending disconnect");
    handle.stop(); handle.stop();
    const int closedQueries = queries;
    QThread::msleep(1050); QCoreApplication::processEvents();
    require(queries == closedQueries && frames == 3, "disconnect retained timer or work");
}

static void shutdown()
{
    QSemaphore entered, release;
    std::atomic<bool> delivered{false};
    std::atomic<quint64> deliveredId{0};
    int queries = 0;
    auto handle = std::make_unique<ProbeHandle>([&](const QByteArray &frame, RxTime) {
        deliveredId = read<quint64>(frame); delivered = true;
    });
    Params params;
    params.sourceKey = "blocking";
    params.capture = [&](RxTime, RxTime) -> std::optional<Input> {
        ++queries; Input input; input.entered = &entered; input.release = &release; return input;
    };
    require(handle->start(params) && until([&] { return handle->isOpen(); }), "blocking open");
    require(handle->send(command(Command::Accumulate, 20)) && until([&] { return entered.available() > 0; }), "generator entry");
    require(handle->send(command(Command::Stop, 20)) && handle->send(command(Command::Accumulate, 21)), "in-flight cancel/retry");
    std::jthread unblock([&] { std::this_thread::sleep_for(50ms); release.release(); });
    handle.reset(); // Owner does not process events while waiting for worker.
    require(delivered && deliveredId == 20 && queries == 1, "close lost in-flight identity or waited for owner capture");
    QCoreApplication::processEvents();
    require(queries == 1, "late timer queried destroyed owner");
    for (int i = 0; i < 40; ++i)
    {
        ProbeHandle immediate([](const QByteArray &, RxTime) {});
        Params p; p.capture = [](RxTime, RxTime) -> std::optional<Input> { return {}; };
        require(immediate.start(p), "immediate start"); immediate.stop();
        require(!immediate.isOpen() && !immediate.isRunning(), "open/stop did not finish");
    }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const quint64 id = 0x123456789ABCDEF0ULL;
    const auto header = SimCardWire::encode(id);
    require(header.size() == SimCardWire::headerBytes && SimCardWire::decode<quint64>(header) == id,
            "result request header changed");
    windowsAndState(); shutdown();
    require(Generator::created == Generator::destroyed, "connection or generator leaked");
    std::cout << "PASS: typed move-only input, fixed windows, persistent generator, cancel/reconnect/shutdown\n";
}
#include "probe.moc"
