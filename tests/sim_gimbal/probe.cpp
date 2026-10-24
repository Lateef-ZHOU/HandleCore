#include <SimGimbalHandle.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Command = SimGimbalWire::Command;
using Report = SimGimbalWire::Report;
static void require(const bool value, const char *what)
{
    if (!value) { std::cerr << what << '\n'; std::exit(1); }
}
template<class F> static bool until(F ready, const int timeoutMs = 2000)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}
static QByteArray command(Command::Kind kind, double pan = 0, double tilt = 0, MotionLimits speed = {})
{
    return SimGimbalWire::encode(Command{kind, pan, tilt, speed});
}
template<class T> concept WireEncodable = requires(T value) { SimGimbalWire::encode(value); };
static_assert(WireEncodable<Command> && WireEncodable<Report> && !WireEncodable<QString> && !WireEncodable<int>);

static void rejectedConnections()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<SimGimbalParams> invalid;
    auto bad = [&](auto change) { SimGimbalParams p; change(p); invalid.push_back(p); };
    bad([&](auto &p) { p.startPan = nan; });
    bad([&](auto &p) { p.startTilt = inf; });
    bad([&](auto &p) { p.limits.panSpeedDps = -1; });
    bad([&](auto &p) { p.limits.tiltSpeedDps = nan; });
    bad([&](auto &p) { p.limits.panSpeedDps = inf; });
    bad([](auto &p) { p.tick = 0ms; });
    bad([](auto &p) { p.tick = -1ms; });
    bad([](auto &p) { p.tick = std::chrono::milliseconds{static_cast<long long>(std::numeric_limits<int>::max()) + 1}; });
    for (const auto &p : invalid)
    {
        std::atomic<int> frames{0}, logs{0}, opened{0};
        SimGimbalHandle handle([&](const QByteArray &, RxTime) { ++frames; });
        QObject::connect(&handle, &SimGimbalHandle::logMessage, &handle,
                         [&](const QString &) { ++logs; }, Qt::DirectConnection);
        QObject::connect(&handle, &SimGimbalHandle::connectionChanged, &handle,
                         [&](bool value) { if (value) ++opened; }, Qt::DirectConnection);
        require(handle.start(p), "start must retain asynchronous acceptance contract");
        require(until([&] { return !handle.isRunning(); }), "invalid open did not finish");
        handle.stop();
        require(!handle.isOpen() && frames == 0 && opened == 0 && logs >= 1, "invalid params created a connection");
    }
}

static void traffic()
{
    std::mutex gate;
    Report last{};
    std::atomic<int> frames{0};
    std::atomic<bool> workerDelivery{true}, validTime{true};
    QThread *owner = QThread::currentThread();
    const auto began = std::chrono::system_clock::now();
    SimGimbalHandle handle([&](const QByteArray &raw, RxTime at) {
        const auto report = SimGimbalWire::decode<Report>(raw);
        require(report.has_value(), "bad report format");
        if (QThread::currentThread() == owner) workerDelivery = false;
        if (at < began || at > std::chrono::system_clock::now()) validTime = false;
        { const std::lock_guard lock(gate); last = *report; }
        ++frames;
    });
    int connections = 0;
    QObject::connect(&handle, &SimGimbalHandle::connectionChanged, &handle, [&](bool up) {
        require(QThread::currentThread() == owner, "connection notification not on owner");
        if (up) { ++connections; require(handle.send(command(Command::SetSpeed, 0, 0, {12000, 12000})), "send on connection notification"); }
    });
    auto arrived = [&](double pan, double tilt) {
        return until([&] { const std::lock_guard lock(gate); return frames > 0 &&
            std::abs(last.pan - pan) < 1e-8 && std::abs(last.tilt - tilt) < 1e-8; });
    };
    require(!handle.send(command(Command::Stop)), "offline send accepted");
    SimGimbalParams params{350, -170, 2ms, {0, 0}};
    require(handle.start(params) && until([&] { return connections == 1 && frames > 0; }), "open/report");
    require(arrived(350, -170), "initial pose");
    require(handle.send(command(Command::GoTo, 370, -190)) && arrived(10, 170), "wrap target delivery");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto &payload : {
             QByteArray("short"), command(Command::Stop) + 'x',
             command(static_cast<Command::Kind>(99)), command(Command::GoTo, nan, 0),
             command(Command::GoTo, 0, inf), command(Command::SetSpeed, 0, 0, {-1, 1}),
             command(Command::SetSpeed, 0, 0, {1, inf}), command(Command::SetSpeed, 0, 0, {nan, 1})})
        require(!handle.send(payload), "invalid command accepted");
    params.limits = {1, 1};
    require(!handle.start(params) && arrived(10, 170) && connections == 1, "speed alone must not reconnect/reset");
    require(handle.send(command(Command::SetSpeed, nan, nan, {0, 0})), "unused target fields should not reject speed");
    require(handle.send(command(Command::GoTo, 80, -50)), "zero-speed aim");
    const int before = frames;
    require(until([&] { return frames >= before + 5; }) && arrived(10, 170), "zero speed moved axes");
    require(handle.send(command(Command::Stop, nan, nan, {nan, nan})), "stop must ignore unused fields");
    require(handle.send(command(Command::SetSpeed, 0, 0, {12000, 12000})), "resume limits");
    const int stopped = frames;
    require(until([&] { return frames >= stopped + 5; }) && arrived(10, 170), "stop did not clear target");
    require(handle.send(command(Command::GoTo, 80, -50)) && arrived(80, -50), "retarget after halt");
    handle.stop();
    const int closed = frames;
    handle.stop();
    QCoreApplication::processEvents();
    require(!handle.isOpen() && !handle.isRunning() && frames == closed && workerDelivery && validTime, "synchronous stop/time/worker delivery");
    params.startPan = -10;
    params.startTilt = 190;
    require(handle.start(params) && arrived(350, -170), "reopen initial normalization");
    handle.stop();
}

static void closeDuringCallback()
{
    QSemaphore entered, release;
    std::atomic<bool> first{true}, exited{false};
    auto handle = std::make_unique<SimGimbalHandle>([&](const QByteArray &, RxTime) {
        if (first.exchange(false)) { entered.release(); release.acquire(); exited = true; }
    });
    require(handle->start(SimGimbalParams{0, 0, 1ms, {0, 0}}), "callback fixture start");
    require(entered.tryAcquire(1, 2000), "callback fixture entry");
    std::jthread unblock([&] { std::this_thread::sleep_for(30ms); release.release(); });
    handle.reset(); // No GUI pumping: destructor must wait for the callback and queued quit.
    require(exited, "destructor returned before callback exited");
    for (int i = 0; i < 40; ++i)
    {
        SimGimbalHandle immediate([](const QByteArray &, RxTime) {});
        require(immediate.start({}), "immediate start");
        immediate.stop();
        require(!immediate.isRunning() && !immediate.isOpen(), "stop during open");
    }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    rejectedConnections();
    traffic();
    closeDuringCallback();
    std::cout << "PASS: SimGimbal production I/O, validation, worker delivery and synchronous shutdown\n";
}
