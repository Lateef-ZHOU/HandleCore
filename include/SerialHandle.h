#pragma once
#include <functional>
#include <atomic>
#include <chrono>
#include <concepts>
#include <memory>
#include <mutex>
#include <QByteArray>
#include <QString>

#include "HandleBase.h"
#include "SerialPort.h"

// 成帧器按值随连接创建；feed 同步交出 0..N 帧，不得保存 deliver 或输入视图。
template <typename F>
concept SerialFramer = std::default_initializable<F> && requires(
    F &framer, const QByteArray &chunk, RxTime at, const std::function<void(const QByteArray &, RxTime)> &deliver)
{
    { framer.feed(chunk, at, deliver) } -> std::same_as<void>;
};

template <SerialFramer Framer>
class SerialConn
{
public:
    Q_DISABLE_COPY(SerialConn)

    [[nodiscard]] static std::shared_ptr<SerialConn> open(const SerialParams &params)
    {
        // 0 在 libserialport 中表示无限等待；串口写保持有限超时。
        if (params.baudRate <= 0 || params.writeTimeoutMs == 0)
            return {};
        sp_port *port = nullptr;
        if (sp_get_port_by_name(params.portInfo.portName.toLocal8Bit().constData(), &port) != SP_OK)
            return {};
        const std::shared_ptr<SerialConn> conn(new SerialConn(port, params.writeTimeoutMs), &SerialConn::close);
        const bool ok = sp_open(port, SP_MODE_READ_WRITE) == SP_OK
            && sp_set_baudrate(port, params.baudRate) == SP_OK
            && sp_set_bits(port, qToUnderlying(params.dataBits)) == SP_OK
            && sp_set_parity(port, params.parity) == SP_OK
            && sp_set_stopbits(port, qToUnderlying(params.stopBits)) == SP_OK
            && sp_set_flowcontrol(port, params.flowControl) == SP_OK;
        return ok ? conn : nullptr;
    }

    // 收包线程：read_next 返回任意字节块；协议在同一调用内交完所有完整帧。
    [[nodiscard]] bool loop(const std::function<void(const QByteArray &, RxTime)> &deliver)
    {
        char bytes[kChunkBytes];
        while (!break_.load(std::memory_order_relaxed))
        {
            const int count = sp_blocking_read_next(port_, bytes, sizeof(bytes), kPollMs);
            if (count < 0)
                return false;
            if (count > 0)
            {
                const auto at = std::chrono::system_clock::now();
                const QByteArray chunk = QByteArray::fromRawData(bytes, count);
                framer_.feed(chunk, at, deliver);
            }
        }
        return true;
    }

    void breakLoop() noexcept { break_.store(true, std::memory_order_relaxed); }

    // 主动发送与 HandleBase 排队的 Reject 回包共用所属线程写口；保留连接级写互斥。
    // 完整写入才成功；error 仅在返回 false 时写入并供调用方记录日志。
    [[nodiscard]] bool send(const QByteArray &data, QString &error)
    {
        const std::lock_guard lock(writeGate_);
        const int written = sp_blocking_write(port_, data.constData(), static_cast<size_t>(data.size()), writeTimeoutMs_);
        if (written < 0)
        {
            error = QStringLiteral("串口发送失败，错误码 %1").arg(written);
            return false;
        }
        if (written != data.size())
        {
            error = QStringLiteral("串口发送超时，仅写入 %1/%2 字节").arg(written).arg(data.size());
            return false;
        }
        return true;
    }

private:
    SerialConn(sp_port *port, const unsigned writeTimeoutMs)
        : port_(port), writeTimeoutMs_(writeTimeoutMs) {}
    ~SerialConn() = default;

    static void close(SerialConn *conn)
    {
        sp_close(conn->port_);
        sp_free_port(conn->port_);
        delete conn;
    }

    static constexpr int kChunkBytes = 256;
    static constexpr unsigned kPollMs = 100;
    sp_port *const port_;
    const unsigned writeTimeoutMs_;
    std::atomic<bool> break_{false};
    std::mutex writeGate_;
    Framer framer_; // 仅收包线程访问；连接释放即清除协议缓冲和时间记录。
};

// 通用串口层只实现连接及 I/O；协议必须实现 RX/TX 和通知，末端析构首句 stop。
template <SerialFramer Framer>
class SerialHandle : public Handle<SerialParams, SerialConn<Framer>>
{
protected:
    ~SerialHandle() override = default;
    [[nodiscard]] virtual RXRuling onRX(const QByteArray &frame, RxTime at,
                                       std::shared_ptr<SerialConn<Framer>> conn) override = 0;
    [[nodiscard]] virtual TXRuling onTX(const QByteArray &payload,
                                       std::shared_ptr<SerialConn<Framer>> conn) const override = 0;

private:
    [[nodiscard]] std::shared_ptr<SerialConn<Framer>> connOpen(const SerialParams &params) final
    {
        return SerialConn<Framer>::open(params);
    }

    [[nodiscard]] bool connLoop(std::shared_ptr<SerialConn<Framer>> conn,
        std::function<void(std::shared_ptr<SerialConn<Framer>>, const QByteArray &, RxTime)> sink) final
    {
        return conn->loop([&conn, &sink](const QByteArray &frame, const RxTime at) { sink(conn, frame, at); });
    }

    void connBreak(std::shared_ptr<SerialConn<Framer>> conn) const final
    {
        if (conn)
            conn->breakLoop();
    }

    [[nodiscard]] bool connSend(std::shared_ptr<SerialConn<Framer>> conn, const QByteArray &data) const final
    {
        if (!conn)
        {
            Q_EMIT this->logMessage(QStringLiteral("设备未连接，无法发送数据"));
            return false;
        }
        QString error;
        if (!conn->send(data, error))
        {
            Q_EMIT this->logMessage(error);
            return false;
        }
        return true;
    }
};
