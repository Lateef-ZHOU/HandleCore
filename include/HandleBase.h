#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <QByteArray>
#include <QMetaObject>
#include <QSemaphore>
#include <QString>
#include <QThread>
#include "ResourcePhase.h"

// 接收时刻按值交付；Qt 元类型与 QML 登记由使用方按需提供。
using RxTime = std::chrono::system_clock::time_point;

// 每次 onRX 只返回一个动作和一份产物；Decode 才使用 at。
// Decode 上交数据，Reject 仅回发响应，Drop 不上交也不回发。
struct RXRuling : QByteArray
{
    enum
    {
        Decode,
        Reject,
        Drop
    } act = Drop;

    RxTime at{};
};

// Encode 将产物交给 connSend，Drop 拒发；发送结果沿用 connSend。
struct TXRuling : QByteArray
{
    enum
    {
        Encode,
        Drop
    } act = Drop;
};

// 连接机制只依赖 Qt Core。所有者线程调用 start/send/stop，run 在收包线程执行。
// conn 四口管理资源与 I/O，onRX/onTX 决定协议动作，具体类提供通知实现。
// 生命周期入口为 start/stop，不使用继承的 QThread 启停接口替代它们。
// 所有者清理回调依赖前须同步 stop；具体 Handle 析构须在拆除派生状态前 stop。
template <typename ParamT, typename ConnT>
class Handle : public QThread
{
public:
    // 已连接且参数不同则重启；运行中其余请求拒收。true 仅表示接受启动参数。
    bool start(const ParamT &params)
    {
        if (isOpen() && params_ != params)
            stop();
        if (isRunning())
            return false;
        params_ = params;
        QThread::start();
        return true;
    }

    // 所属线程：先同步通知关闭，让接收者在连接仍可用时发送收尾命令，再促断并等待。
    // 通知接收者不得递归启停本 Handle；重复 stop 不重复通知。
    void stop() noexcept
    {
        if (QThread::currentThread() == this)
            qFatal("Handle cannot wait for its own receive callback");
        if (QThread::currentThread() != thread())
            qFatal("Handle stop must run on its owning thread");
        if (!isRunning())
        {
            if (!wait())
                qFatal("Handle did not finish");
            return;
        }
        connectionChanged(HandleCore::ResourcePhase::AboutToRelease);
        stopGate_.release();
        connBreak(conn_.load().lock());
        if (!wait())
            qFatal("Handle receive callback did not finish");
        stopGate_.tryAcquire(stopGate_.available());
    }

    // 任意线程观察已发布的连接是否仍然存活，不延长其寿命。
    [[nodiscard]] bool isOpen() const { return !conn_.load().expired(); }

    // 所属线程：只取得一次连接份额；onTX 决定动作，connSend 检查连接并返回结果。
    bool send(const QByteArray &payload) const
    {
        const auto conn = conn_.load().lock();
        const TXRuling r = onTX(payload, conn);
        switch (r.act)
        {
        case TXRuling::Drop:
            return false;
        case TXRuling::Encode:
            return connSend(conn, r);
        }
        return false;
    }

protected:

    // 收包线程同步上传协议产物；实现可覆写处理或由具体类声明为信号。
    // 字节可能借用底层缓冲，异步留存须取得所有权；依赖状态清理前必须 stop。
    virtual void onData(const QByteArray &data, RxTime at) = 0;

    // 派生状态须已由具体类/所有者保护；本层等待不替代提前 stop。
    ~Handle() override { wait(); }

    // AboutToRelease 仅由主动 stop 在所属线程同步发出；收尾槽不得排队、重入启停或泵事件。
    // 其余阶段由工作线程发出。每次打开（含重连）先 AboutToAcquire，发布后 Acquired，
    // 撤销后 Released；打开失败只报告失败，不伪造已获取/已释放事件。
    // 自动重连不发 AboutToRelease，不撤销上层任务；完成通知只用于回读现状。
    // 基类只约束函数；需要 QObject::connect 的具体类以 Q_OBJECT/Q_SIGNALS 实现。
    virtual void connectionChanged(HandleCore::ResourcePhase phase) = 0;

    // 可从调用线程或收包线程发射；接收方决定显示、记录及线程接线。
    virtual void logMessage(const QString &text) const = 0;

private:

    // 已接受的重连参数：仅在线程停止时由 start 写入，run 只读。
    ParamT params_;

    // 首次打开失败即结束；后续 I/O 错误按一秒间隔重试。
    // 先发布连接再检查停止请求，覆盖打开期间到达的 stop。
    void run() final
    {
        connectionChanged(HandleCore::ResourcePhase::AboutToAcquire);
        auto h = connOpen(params_);
        if (!h)
        {
            Q_EMIT logMessage("设备连接失败");
            return;
        }
        for (int i = 1;; ++i)
        {
            if (h)
            {
                conn_.store(h);

                const bool stopping = stopGate_.tryAcquire();
                bool clean = true;
                if (!stopping)
                {
                    connectionChanged(HandleCore::ResourcePhase::Acquired);
                    if (i == 1)
                        Q_EMIT logMessage("设备连接成功");
                    clean = connLoop(h, makeDispatch(this));
                }
                // 先撤观察口，再释放本地资源。排队消息中的旧份额不再使 isOpen 为真。
                conn_.store({});
                h.reset();
                connectionChanged(HandleCore::ResourcePhase::Released);

                if (clean)
                    break;
            }
            Q_EMIT logMessage(QString("设备连接断开，1 秒后尝试第 %1 次重连").arg(i));
            if (stopGate_.tryAcquire(1, 1000))
                break;
            connectionChanged(HandleCore::ResourcePhase::AboutToAcquire);
            h = connOpen(params_);
        }

        Q_EMIT logMessage("设备连接断开");
    }

    // 工作线程打开连接；资源由返回份额及删除器管理，失败由实现自行清理。
    [[nodiscard]] virtual std::shared_ptr<ConnT> connOpen(const ParamT &params) = 0;

    // 工作线程阻塞收包：true 正常结束，false 表示 I/O 错误、需要重连。
    // 持有连接，同步交入每份字节及其接收时刻。
    [[nodiscard]] virtual bool connLoop(std::shared_ptr<ConnT> conn,
        std::function<void(std::shared_ptr<ConnT>, const QByteArray &, RxTime)> sink) = 0;

    // 所属线程打断收包并撤销待处理请求；打开期间/重连间隙可能传空连接。
    virtual void connBreak(std::shared_ptr<ConnT> h) const = 0;

    // 所属线程执行主动发送与排队应答；实现负责判空、失败日志及实际传输。
    [[nodiscard]] virtual bool connSend(std::shared_ptr<ConnT> h, const QByteArray &data) const = 0;

    // onRX 在收包线程执行，连接非空，产物时刻由协议决定。
    // onTX 在所属线程执行，连接可能为空；两者不读取重连参数种子。
    [[nodiscard]] virtual RXRuling onRX(const QByteArray &raw, RxTime at, std::shared_ptr<ConnT> conn) = 0;
    [[nodiscard]] virtual TXRuling onTX(const QByteArray &payload, std::shared_ptr<ConnT> conn) const = 0;

    // 每轮收包生成派发回调，由 connLoop 接收；本层不保存固定闭包副本。
    [[nodiscard]] static std::function<void(std::shared_ptr<ConnT>, const QByteArray &, RxTime)> makeDispatch(Handle *self);

    // 停止请求跨打开/收包/重连等待保留，线程退出后由 stop 清空。
    QSemaphore stopGate_;

    // 观察口不持有资源；收包和发送在使用期间各自持有连接份额。
    std::atomic<std::weak_ptr<ConnT>> conn_;
};

template <typename ParamT, typename ConnT>
std::function<void(std::shared_ptr<ConnT>, const QByteArray &, RxTime)>
Handle<ParamT, ConnT>::makeDispatch(Handle<ParamT, ConnT> *self)
{
    return [self](std::shared_ptr<ConnT> conn, const QByteArray &raw, const RxTime at)
    {
        RXRuling r = self->onRX(raw, at, conn);
        switch (r.act)
        {
        case RXRuling::Decode:
            self->onData(r, r.at);
            break;
        case RXRuling::Reject:
            // 裁决可能借用接收缓冲；排队前取得字节所有权，连接随队列项保活。
            r.detach();
            QMetaObject::invokeMethod(self, [self, conn, r] {
                (void)self->connSend(conn, r);
            }, Qt::QueuedConnection);
            break;
        case RXRuling::Drop:
            break;
        }
    };
}

// 检查公开继承关系，不约束 Qt 元对象登记方式。
template <typename H>
concept HandleLike = requires(H *handle)
{
    []<typename P, typename C>(Handle<P, C> *) {}(handle);
};
