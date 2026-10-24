#pragma once
#include <chrono>
#include <limits>
#include <memory>
#include <QByteArray>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include "HandleBase.h"
#include "SimGimbalProtocol.h"

// 命令在所属线程校验并排队；电机、节拍与连接事件循环仅在工作线程访问。
// 回报使用真实流逝时间推进，接收时刻为 system_clock；同步关闭沿用 Handle.stop。
// 连接参数只定义起点、回报节拍和初始限速。
// 限速不参与同参比较：运行中由 SetSpeed 修改，再次连接不应因调速而复位。
struct SimGimbalParams
{
    double startPan = 0.0;
    double startTilt = 0.0;
    std::chrono::milliseconds tick{20};
    MotionLimits limits{0.0, 0.0};
    [[nodiscard]] bool operator==(const SimGimbalParams &o) const
    {
        return startPan == o.startPan && startTilt == o.startTilt && tick == o.tick;
    }
};

// 一次连接的状态：住句柄线程的 QObject。成员全部只在句柄线程上碰（命令与停机都经队列投到这里兑现）。
class SimGimbalConn final : public QObject
{
public:
    explicit SimGimbalConn(const SimGimbalParams &p) : motor({p.startPan, p.startTilt}, p.limits), tick(p.tick) {}
    GimbalMotion motor;
    const std::chrono::milliseconds tick;
    QTimer timer;                                // 节拍
    QEventLoop loop;                             // connLoop 跑它；connBreak 让它退出
    std::chrono::steady_clock::time_point last{}; // 上一拍时刻：电机按真实流逝推进，不假定定时器准点
};

class SimGimbalHandle final : public Handle<SimGimbalParams, SimGimbalConn>
{
    Q_OBJECT
public:
    using Handle::Handle;
    ~SimGimbalHandle() override { stop(); }
Q_SIGNALS:
    void connectionChanged(bool connected) override;
    void logMessage(const QString &text) const final;

private:
    [[nodiscard]] std::shared_ptr<SimGimbalConn> connOpen(const SimGimbalParams &p) override
    {
        if (!GimbalMotion::Pose{p.startPan, p.startTilt}.isFinite() || !p.limits.isValid())
        {
            Q_EMIT logMessage(QStringLiteral("虚拟云台初始角度须有限，限速须有限且非负"));
            return {};
        }
        // QTimer 的毫秒入口使用 int；拒绝截断、零周期和负周期。
        if (p.tick.count() <= 0 || p.tick.count() > std::numeric_limits<int>::max())
        {
            Q_EMIT logMessage(QStringLiteral("虚拟云台回报周期须为 1..INT_MAX 毫秒"));
            return {};
        }
        return std::make_shared<SimGimbalConn>(p);
    }

    // 句柄线程：起节拍、跑事件循环直到停机。节拍槽捕获弱份额（连接对象持定时器，定时器持槽，强份额会成环）。
    [[nodiscard]] bool connLoop(std::shared_ptr<SimGimbalConn> conn, Sink sink) override
    {
        conn->last = std::chrono::steady_clock::now();
        QObject::connect(&conn->timer, &QTimer::timeout, conn.get(), [weak = std::weak_ptr(conn), sink] {
            const auto c = weak.lock();
            if (!c)
                return;
            const auto now = std::chrono::steady_clock::now();
            c->motor.step(std::chrono::duration<double>(now - c->last).count());
            c->last = now;
            const auto &p = c->motor.pose();
            sink(c, SimGimbalWire::encode(SimGimbalWire::Report{p.pan, p.tilt}), std::chrono::system_clock::now());
        });
        conn->timer.start(conn->tick);
        conn->loop.exec();
        conn->timer.stop();
        return true; // 只有停机会让循环退出：干净退出，不重连
    }

    // 队列投到句柄线程：事件循环还没起来也不丢（排在线程队列里，exec 一开始就处理）。
    void connBreak(std::shared_ptr<SimGimbalConn> h) const final
    {
        if (!h)
            return;
        QMetaObject::invokeMethod(h.get(), [h] { h->loop.quit(); }, Qt::QueuedConnection);
    }

    [[nodiscard]] bool connSend(std::shared_ptr<SimGimbalConn> h, const QByteArray &data) const override
    {
        if (!h)
        {
            Q_EMIT logMessage("设备未连接，无法发送数据");
            return false;
        }
        const auto cmd = SimGimbalWire::decode<SimGimbalWire::Command>(data);
        if (!cmd)
        {
            Q_EMIT logMessage(QStringLiteral("虚拟云台指令格式错误，未发送"));
            return false;
        }
        switch (cmd->kind)
        {
        case SimGimbalWire::Command::GoTo:
            if (GimbalMotion::Pose{cmd->pan, cmd->tilt}.isFinite())
                break;
            Q_EMIT logMessage(QStringLiteral("虚拟云台目标角度须为有限值，未发送"));
            return false;
        case SimGimbalWire::Command::SetSpeed:
            if (cmd->limits.isValid())
                break;
            Q_EMIT logMessage(QStringLiteral("虚拟云台限速须有限且非负，未发送"));
            return false;
        case SimGimbalWire::Command::Stop:
            break;
        default:
            Q_EMIT logMessage(QStringLiteral("虚拟云台指令类型未知，未发送"));
            return false;
        }
        const bool queued = QMetaObject::invokeMethod(
            h.get(),
            [h, c = *cmd] {
                switch (c.kind)
                {
                case SimGimbalWire::Command::GoTo:
                    h->motor.aim({c.pan, c.tilt});
                    break;
                case SimGimbalWire::Command::Stop:
                    h->motor.halt();
                    break;
                case SimGimbalWire::Command::SetSpeed:
                    h->motor.setLimits(c.limits);
                    break;
                }
            },
            Qt::QueuedConnection);
        if (!queued)
            Q_EMIT logMessage(QStringLiteral("虚拟云台指令投递失败"));
        return queued;
    }

    [[nodiscard]] RXRuling onRX(const QByteArray &raw, const RxTime at, std::shared_ptr<SimGimbalConn>) override
    {
        return {RXRuling::Decode, raw, at};
    }
    [[nodiscard]] TXRuling onTX(const QByteArray &payload, std::shared_ptr<SimGimbalConn>) const override
    {
        return {TXRuling::Encode, payload};
    }
};
