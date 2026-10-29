#pragma once
#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <memory>
#include <utility>
#include <QByteArray>
#include <QSemaphore>
#include <QString>
#include <QThread>

// 接收时刻按值交付；Qt 元类型与 QML 登记由使用方按需提供。
using RxTime = std::chrono::system_clock::time_point;

// 收包线程同步调用；字节可能借用底层缓冲，异步留存由接收方取得所有权。
using DataCallback = std::function<void(const QByteArray &data, RxTime at)>;

// 每次 onRX 只返回一个动作和一份产物；Decode 才使用 at。
// Decode 上交数据，Reject 仅回发响应，Drop 不上交也不回发。
struct RXRuling
{
    enum Act
    {
        Decode,
        Reject,
        Drop
    };

    Act act = Drop;
    QByteArray payload;

    RxTime at{};
};

// Encode 将产物交给 connSend，Drop 拒发；发送结果沿用 connSend。
struct TXRuling
{
    enum Act
    {
        Encode,
        Drop
    };

    Act act = Drop;
    QByteArray payload;
};

// 只约束实际使用的默认构造、const 赋值和条件比较；不要求复制构造或完整相等关系。
template <typename P>
concept HandleParameters = std::default_initializable<P> && requires(P &current, const P &incoming)
{
    current = incoming;
    (current != incoming) ? 0 : 1;
};

// 连接机制只依赖 Qt Core。所有者线程调用 start/send/stop，run 在收包线程执行。
// conn 四口管理资源与 I/O，onRX/onTX 决定协议动作，具体类提供通知实现。
// 生命周期入口为 start/stop，不使用继承的 QThread 启停接口替代它们。
// 所有者清理回调依赖前须同步 stop；具体 Handle 析构须在拆除派生状态前 stop。
template <HandleParameters ParamT, typename ConnT>
class Handle : public QThread
{
public:
    using ParamType = ParamT;
    using ConnType = ConnT;

    // connLoop 持有连接，同步交入每份字节及其接收时刻。
    using Sink = std::function<void(std::shared_ptr<ConnT>, const QByteArray &, RxTime)>;
    Q_DISABLE_COPY(Handle)

    // onData 必须可调用，其捕获的状态须存活到 stop 返回。
    explicit Handle(DataCallback onData)
        : onData_(std::move(onData))
    {
    }

    // 已连接且参数不同则重启；运行中其余请求拒收。true 仅表示接受启动参数。
    bool start(const ParamType &params)
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
        connectionChanged(false);
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
            return connSend(conn, r.payload);
        }
        return false;
    }

protected:

    // 派生状态须已由具体类/所有者保护；本层等待不替代提前 stop。
    ~Handle() override { wait(); }

    // true 由工作线程在首连成功时发出；false 由所属线程在 stop 促断前同步发出。
    // 中途重连不通知；false 是关闭前收尾时机，isOpen 在 stop 返回后才保证为 false。
    // 实际线程退出可监听 QThread::finished，通知槽只回读状态，不再次撤单。
    // 基类只约束函数；需要 QObject::connect 的具体类以 Q_OBJECT/Q_SIGNALS 实现。
    virtual void connectionChanged(bool connected) = 0;

    // 可从调用线程或收包线程发射；接收方决定显示、记录及线程接线。
    virtual void logMessage(const QString &text) const = 0;

private:

    // 已接受的重连参数：仅在线程停止时由 start 写入，run 只读。
    ParamT params_;

    // 首次打开失败即结束；后续 I/O 错误按一秒间隔重试。
    // 先发布连接再检查停止请求，覆盖打开期间到达的 stop。
    void run() final
    {
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

                if (stopGate_.tryAcquire())
                    break;
                if (i == 1)
                {
                    connectionChanged(true);
                    Q_EMIT logMessage("设备连接成功");
                }
                const bool clean = connLoop(h, makeDispatch(this));
                h.reset();

                if (clean)
                    break;
            }
            Q_EMIT logMessage(QString("设备连接断开，1 秒后尝试第 %1 次重连").arg(i));
            if (stopGate_.tryAcquire(1, 1000))
                break;
            h = connOpen(params_);
        }

        // 断开通知已由所属线程在促断前发出；此处只释放连接，不排队重复通知。
        h.reset();
        Q_EMIT logMessage("设备连接断开");
    }

    // 工作线程打开连接；资源由返回份额及删除器管理，失败由实现自行清理。
    [[nodiscard]] virtual std::shared_ptr<ConnT> connOpen(const ParamT &params) = 0;

    // 工作线程阻塞收包：true 正常结束，false 表示 I/O 错误、需要重连。
    [[nodiscard]] virtual bool connLoop(std::shared_ptr<ConnT> conn, Sink sink) = 0;

    // 所属线程打断收包并撤销待处理请求；打开期间/重连间隙可能传空连接。
    virtual void connBreak(std::shared_ptr<ConnT> h) const = 0;

    // 所属线程发送与收包线程应答共用；实现负责判空、失败日志与 I/O 并发安全。
    [[nodiscard]] virtual bool connSend(std::shared_ptr<ConnT> h, const QByteArray &data) const = 0;

    // onRX 在收包线程执行，连接非空，产物时刻由协议决定。
    // onTX 在所属线程执行，连接可能为空；两者不读取重连参数种子。
    [[nodiscard]] virtual RXRuling onRX(const QByteArray &raw, RxTime at, std::shared_ptr<ConnT> conn) = 0;
    [[nodiscard]] virtual TXRuling onTX(const QByteArray &payload, std::shared_ptr<ConnT> conn) const = 0;

    // 每轮收包生成派发回调，由 connLoop 接收；本层不保存固定闭包副本。
    [[nodiscard]] static Sink makeDispatch(Handle *self);

    const DataCallback onData_;

    // 停止请求跨打开/收包/重连等待保留，线程退出后由 stop 清空。
    QSemaphore stopGate_;

    // 观察口不持有资源；收包和发送在使用期间各自持有连接份额。
    std::atomic<std::weak_ptr<ConnT>> conn_;
};

template <HandleParameters ParamT, typename ConnT>
typename Handle<ParamT, ConnT>::Sink Handle<ParamT, ConnT>::makeDispatch(Handle<ParamT, ConnT> *self)
{
    return [self](std::shared_ptr<ConnT> conn, const QByteArray &raw, const RxTime at)
    {
        const RXRuling r = self->onRX(raw, at, conn);
        switch (r.act)
        {
        case RXRuling::Decode:
            self->onData_(r.payload, r.at);
            break;
        case RXRuling::Reject:
            (void)self->connSend(conn, r.payload);
            break;
        case RXRuling::Drop:
            break;
        }
    };
}

// 检查公开继承关系，不约束 Qt 元对象登记方式。
template <typename H>
concept HandleLike = std::derived_from<H, Handle<typename H::ParamType, typename H::ConnType>>;
