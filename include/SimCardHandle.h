#pragma once
#include <chrono>
#include <concepts>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include "HandleBase.h"
#include "SimCardProtocol.h"

// 合成器按值随连接构造，不要求可复制/移动；输入只需能移入排队闭包。
template <typename G>
concept SimCardGenerator = std::default_initializable<G> && requires { typename G::Input; }
    && std::move_constructible<typename G::Input>
    && requires(G &generator, quint64 id, RxTime at, const typename G::Input &input)
{
    { generator.synthesize(id, at, at, input) } -> std::same_as<QByteArray>;
};

template <typename Input>
struct SimCardParams
{
    QString sourceKey;
    // 所属线程同步调用，返回自持快照；nullopt 表示缺数据，不安排后台工作。
    std::function<std::optional<Input>(RxTime begin, RxTime end)> capture;
    [[nodiscard]] bool operator==(const SimCardParams &o) const { return sourceKey == o.sourceKey; }
};

template <SimCardGenerator Generator>
class SimCardConn final : public QObject
{
public:
    using Input = typename Generator::Input;
    explicit SimCardConn(const SimCardParams<Input> &p) : params(p) {}
    const SimCardParams<Input> params; // 所属线程只借用只读查询配置，不访问下面的工作状态。

    void run(DataCallback deliver)
    {
        deliver_ = std::move(deliver);
        loop_.exec();
        deliver_ = {};
    }
    void quit() { loop_.quit(); }

    // 所属线程排队；上下文为原连接，关闭后不借新连接，不额外强持连接。
    [[nodiscard]] bool enqueue(quint64 id, RxTime begin, RxTime end, Input input)
    {
        return QMetaObject::invokeMethod(this,
            [this, id, begin, end, input = std::move(input)] {
                const auto frame = generator_.synthesize(id, begin, end, input);
                if (!frame.isEmpty()) deliver_(frame, end);
            }, Qt::QueuedConnection);
    }

private:
    QEventLoop loop_;
    Generator generator_; // 构造、使用、销毁均在 Handle 工作线程，跨请求保留算法状态。
    DataCallback deliver_; // 工作线程进入事件循环前设置，退出后撤销。
};

// 所属线程固定累积窗口并计时，到点取快照；工作线程只执行合成器并交付字节。
// 末端提供 Qt 通知，析构首句调用 stop。模板层不引入 Q_OBJECT 或业务元类型登记。
template <SimCardGenerator Generator>
class SimCardHandle : public Handle<SimCardParams<typename Generator::Input>, SimCardConn<Generator>>
{
    using Base = Handle<SimCardParams<typename Generator::Input>, SimCardConn<Generator>>;
public:
    using typename Base::ParamType;
    using typename Base::ConnType;
    explicit SimCardHandle(DataCallback onData) : Base(std::move(onData))
    {
        due_.setSingleShot(true);
        due_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&due_, &QTimer::timeout, this, [this] { capture(); });
    }

protected:
    ~SimCardHandle() override = default;

private:
    struct Pending
    {
        std::weak_ptr<ConnType> connection;
        quint64 requestId;
        RxTime begin;
        RxTime end;
    };
    // const 来自 Handle 的发送契约；只在所属线程访问。
    mutable QTimer due_;
    mutable std::optional<Pending> pending_;

    void capture()
    {
        const auto pending = std::exchange(pending_, std::nullopt);
        if (!pending) return;
        const auto connection = pending->connection.lock();
        if (!connection) return;
        auto input = connection->params.capture(pending->begin, pending->end);
        if (!input) return;
        if (!connection->enqueue(pending->requestId, pending->begin, pending->end, std::move(*input)))
            Q_EMIT this->logMessage(QStringLiteral("虚拟采集任务投递失败"));
    }

    [[nodiscard]] std::shared_ptr<ConnType> connOpen(const ParamType &params) final
    {
        if (!params.capture)
        {
            Q_EMIT this->logMessage(QStringLiteral("虚拟采集未提供取数函数，无法连接"));
            return {};
        }
        return std::make_shared<ConnType>(params);
    }

    [[nodiscard]] bool connLoop(std::shared_ptr<ConnType> connection, typename Base::Sink sink) final
    {
        connection->run([weak = std::weak_ptr(connection), sink = std::move(sink)](const QByteArray &frame, RxTime at) {
            if (const auto c = weak.lock()) sink(c, frame, at);
        });
        return true;
    }

    void connBreak(std::shared_ptr<ConnType> connection) const final
    {
        due_.stop();
        pending_.reset();
        if (connection) QMetaObject::invokeMethod(connection.get(), &ConnType::quit, Qt::QueuedConnection);
    }

    [[nodiscard]] bool connSend(std::shared_ptr<ConnType> connection, const QByteArray &data) const final
    {
        Q_ASSERT(QThread::currentThread() == this->thread());
        if (!connection)
        {
            Q_EMIT this->logMessage(QStringLiteral("设备未连接，无法发送数据"));
            return false;
        }
        const auto command = SimCardWire::decode<SimCardWire::Command>(data);
        if (!command) return false;
        if (command->kind == SimCardWire::Command::Stop)
        {
            if (pending_ && pending_->requestId == command->requestId)
            {
                due_.stop();
                pending_.reset();
            }
        }
        else if (command->kind == SimCardWire::Command::Accumulate && command->accumSec > 0
                 && command->accumSec <= std::numeric_limits<int>::max() / 1000)
        {
            const auto begin = std::chrono::system_clock::now();
            pending_ = Pending{connection, command->requestId, begin, begin + std::chrono::seconds(command->accumSec)};
            due_.start(command->accumSec * 1000);
        }
        else return false;
        return true;
    }

    [[nodiscard]] RXRuling onRX(const QByteArray &raw, RxTime at, std::shared_ptr<ConnType>) final
    { return {RXRuling::Decode, raw, at}; }
    [[nodiscard]] TXRuling onTX(const QByteArray &payload, std::shared_ptr<ConnType>) const final
    { return {TXRuling::Encode, payload}; }
};
