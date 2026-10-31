#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <utility>
#include <QByteArray>
#include <QMetaObject>
#include <pcap.h>
#include "HandleBase.h"
#include "PcapInterfaces.h"

// 接受后的值快照用于打开和重连；ARP 学习值不回写参数。
struct PcapParams
{
    NetworkInterfaceInfo host;
    IPv4 hostIPv4;
    IPv4 deviceIPv4;
    MAC deviceMAC;
    quint16 hostPort = 0;
    quint16 devicePort = 0;
    std::chrono::milliseconds readTimeout{100};
    int kernelBufferBytes = 1 << 20;
    bool operator==(const PcapParams &) const = default;
};

// 学习 MAC 与摘走最新挂起报文必须是同一个原子操作。
struct ArpState
{
    MAC deviceMAC;
    QByteArray pending;
    ArpState(MAC mac, QByteArray bytes) : deviceMAC(std::move(mac)), pending(std::move(bytes)) {}
};

struct PcapConn
{
    pcap_t *const p;
    const PcapParams params;
    std::atomic<std::shared_ptr<const ArpState>> arp;
    static void close(PcapConn *conn)
    {
        pcap_close(conn->p);
        delete conn;
    }
};

// 单连接以太网 UDP Handle。worker 读包并裁决；所属线程执行所有实际发送。
// 单句柄并发读写是本模块的约定，不宣称 libpcap 对此提供跨平台线程安全保证。
class PcapHandle final : public Handle<PcapParams, PcapConn>
{
    Q_OBJECT
public:
    using Handle::Handle;
    ~PcapHandle() override { stop(); }

    bool start(const PcapParams &params)
    {
        if (Handle::start(params)) return true;
        Q_EMIT logMessage(QStringLiteral("连接线程已在运行，本次请求未提交；更换参数请先断开再连接"));
        return false;
    }

Q_SIGNALS:
    void connectionChanged(bool connected) override;
    void logMessage(const QString &text) const final;

private:
    [[nodiscard]] std::shared_ptr<PcapConn> connOpen(const PcapParams &params) override;

    // BPF 筛选接收四元组及本机 ARP；裁决只识别种类。
    [[nodiscard]] static Filter buildFilter(const PcapParams &params)
    {
        return ARPPacket::matchARP({}, {}, {}, {}, {}, {}, params.hostIPv4) |
               UDPDatagram::matchUDP({}, {}, params.deviceIPv4, params.hostIPv4,
                                     params.devicePort, params.hostPort);
    }

    [[nodiscard]] bool connLoop(std::shared_ptr<PcapConn> conn, Sink sink) override
    {
        DataCallback feed = [&conn, &sink](const QByteArray &frame, RxTime at) { sink(conn, frame, at); };
        return pcap_loop(conn->p, -1, onPacket, reinterpret_cast<u_char *>(&feed)) == PCAP_ERROR_BREAK;
    }

    void connBreak(std::shared_ptr<PcapConn> conn) const final
    {
        if (conn) pcap_breakloop(conn->p);
    }

    [[nodiscard]] bool connSend(std::shared_ptr<PcapConn> conn, const QByteArray &data) const final
    {
        if (!conn)
        {
            Q_EMIT logMessage(QStringLiteral("设备未连接，无法发送数据"));
            return false;
        }
        if (QThread::currentThread() != thread())
        {
            // 深拷贝跨过 pcap 缓冲寿命；只弱借原连接，停止/重连后不得复用新连接。
            const std::weak_ptr<PcapConn> original = conn;
            return QMetaObject::invokeMethod(const_cast<PcapHandle *>(this),
                [this, original, bytes = QByteArray(data.constData(), data.size())] {
                    if (const auto live = original.lock()) (void)connSend(live, bytes);
                }, Qt::QueuedConnection);
        }
        if (pcap_sendpacket(conn->p, reinterpret_cast<const u_char *>(data.constData()),
                            static_cast<int>(data.size())) != 0)
        {
            Q_EMIT logMessage(QStringLiteral("网口发送失败：%1").arg(QString::fromLocal8Bit(pcap_geterr(conn->p))));
            return false;
        }
        return true;
    }

    // pcap 缓冲仅在同步回调期间有效；采集时刻保留内核时间戳。
    static void onPacket(u_char *user, const pcap_pkthdr *header, const u_char *packet)
    {
        if (header->caplen != header->len) return;
        const auto view = QByteArray::fromRawData(reinterpret_cast<const char *>(packet), header->caplen);
        (*reinterpret_cast<DataCallback *>(user))(view,
            std::chrono::system_clock::from_time_t(header->ts.tv_sec) +
            std::chrono::microseconds{header->ts.tv_usec});
    }

    [[nodiscard]] RXRuling onRX(const QByteArray &raw, const RxTime at, std::shared_ptr<PcapConn> conn) override
    {
        switch (kindOf(raw)) // conn 因果必非空（句柄→loop→dispatch），本口自解不判
        {
        case FrameKind::UDPDatagram:
            return {RXRuling::Decode, UDPDatagram(raw).bodyView(), at};
        case FrameKind::ARPRequest:
            return {RXRuling::Reject, ARPReply(ARPRequest(raw), conn->params.host.macAddress)};
        case FrameKind::ARPReply:
        {
            const ARPReply reply(raw);
            if (reply.senderIP() != conn->params.deviceIPv4)
                return {};
            const MAC mac = reply.senderMAC();
            const auto old = conn->arp.exchange(std::make_shared<const ArpState>(mac, QByteArray{}));
            if (!old || old->pending.isEmpty())
                return {}; // 纯学习，无挂起报文
            return {RXRuling::Reject,
                    UDPDatagram(mac, conn->params.host.macAddress, conn->params.hostIPv4, conn->params.deviceIPv4,
                                conn->params.hostPort, conn->params.devicePort, old->pending)};
        }
        default:
            return {}; // 完备性兜底：不认识的种类 / 结构长度不足 / 垫底帧——Drop
        }
    }

    [[nodiscard]] TXRuling onTX(const QByteArray &payload, std::shared_ptr<PcapConn> conn) const override
    {
        if (!conn)
        {
            Q_EMIT logMessage("设备未连接，无法发送数据");
            return {}; // 未连接：无 conn 可组包
        }
        auto s = conn->arp.load();
        if (s && !s->deviceMAC.isNull()) // 对端 MAC 已知：直接组 UDP
            return {TXRuling::Encode,
                    UDPDatagram(s->deviceMAC, conn->params.host.macAddress, conn->params.hostIPv4, conn->params.deviceIPv4,
                                conn->params.hostPort, conn->params.devicePort, payload)};
        const auto stash = std::make_shared<const ArpState>(MAC{}, payload);
        if (conn->arp.compare_exchange_strong(s, stash))
        {
            Q_EMIT logMessage("对端 MAC 未知，报文已暂存，正在探测");
            return {TXRuling::Encode, ARPRequest(conn->params.host.macAddress, conn->params.hostIPv4, conn->params.deviceIPv4)};
        }
        return {TXRuling::Encode,
                UDPDatagram(s->deviceMAC, conn->params.host.macAddress, conn->params.hostIPv4, conn->params.deviceIPv4,
                            conn->params.hostPort, conn->params.devicePort, payload)};
    }
};

inline std::shared_ptr<PcapConn> PcapHandle::connOpen(const PcapParams &params)
{
    char error[PCAP_ERRBUF_SIZE]{};
    pcap_t *raw = pcap_create(params.host.pcapDeviceName.toLocal8Bit().constData(), error);
    if (!raw)
    {
        Q_EMIT logMessage(QStringLiteral("网卡打开失败：%1").arg(QString::fromLocal8Bit(error)));
        return {};
    }
    // create 之后立即接管资源；所有失败路径均由同一个删除器释放。
    auto conn = std::shared_ptr<PcapConn>(new PcapConn{raw, params, {}}, &PcapConn::close);
    if (pcap_set_snaplen(raw, 65536) != 0 || pcap_set_promisc(raw, 1) != 0 ||
        pcap_set_timeout(raw, static_cast<int>(params.readTimeout.count())) != 0 ||
        pcap_set_buffer_size(raw, params.kernelBufferBytes) != 0 || pcap_activate(raw) < 0)
    {
        Q_EMIT logMessage(QStringLiteral("网卡配置或激活失败：%1").arg(QString::fromLocal8Bit(pcap_geterr(raw))));
        return {};
    }
    if (!params.deviceMAC.isNull())
        conn->arp.store(std::make_shared<const ArpState>(params.deviceMAC, QByteArray{}));
    bpf_program program{};
    if (pcap_compile(raw, &program, buildFilter(params).toLocal8Bit().constData(), 1, PCAP_NETMASK_UNKNOWN) < 0)
    {
        Q_EMIT logMessage(QStringLiteral("网卡过滤器编译失败：%1").arg(QString::fromLocal8Bit(pcap_geterr(raw))));
        return {};
    }
    const bool ok = pcap_setfilter(raw, &program) == 0;
    pcap_freecode(&program);
    if (!ok)
        Q_EMIT logMessage(QStringLiteral("网卡过滤器安装失败：%1").arg(QString::fromLocal8Bit(pcap_geterr(raw))));
    return ok ? conn : nullptr;
}
