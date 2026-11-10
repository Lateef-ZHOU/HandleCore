#pragma once
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <QByteArray>
#include <QMetaObject>
#include <boost/circular_buffer.hpp>
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

struct PcapConn
{
    pcap_t *const p;
    const PcapParams params;
    // 发布前初始化；发布后仅所属线程学习 MAC、暂存和取走待发报文。
    // MAC 未知时按到达顺序暂存，满后覆盖最旧一条；学习后从旧到新补发。
    static constexpr std::size_t kPendingCapacity = 32;
    MAC deviceMAC;
    boost::circular_buffer<QByteArray> pending{kPendingCapacity};
    static void close(PcapConn *conn)
    {
        pcap_close(conn->p);
        delete conn;
    }
};

// 单连接以太网 UDP Handle。worker 读包并裁决；所属线程执行所有实际发送。
// 单句柄并发读写是本模块的约定，不宣称 libpcap 对此提供跨平台线程安全保证。
class PcapHandle : public Handle<PcapParams, PcapConn>
{
    Q_OBJECT
public:
    using Handle::Handle;
    ~PcapHandle() override = default;

Q_SIGNALS:
    void connectionChanged(HandleCore::ResourcePhase connected) override;
    void logMessage(const QString &text) const final;

private:
    [[nodiscard]] std::shared_ptr<PcapConn> connOpen(const PcapParams &params) override
    {
        char error[PCAP_ERRBUF_SIZE]{};
        pcap_t *raw = pcap_create(params.host.pcapDeviceName.toLocal8Bit().constData(), error);
        if (!raw)
        {
            Q_EMIT logMessage(QStringLiteral("网卡打开失败：%1").arg(QString::fromLocal8Bit(error)));
            return {};
        }
        // create 之后立即接管资源；所有失败路径均由同一个删除器释放。
        auto conn = std::shared_ptr<PcapConn>(new PcapConn{raw, params, params.deviceMAC}, &PcapConn::close);
        if (pcap_set_snaplen(raw, 65536) != 0 || pcap_set_promisc(raw, 1) != 0 ||
            pcap_set_timeout(raw, static_cast<int>(params.readTimeout.count())) != 0 ||
            pcap_set_buffer_size(raw, params.kernelBufferBytes) != 0 || pcap_activate(raw) < 0)
        {
            Q_EMIT logMessage(QStringLiteral("网卡配置或激活失败：%1").arg(QString::fromLocal8Bit(pcap_geterr(raw))));
            return {};
        }
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

    // BPF 筛选接收四元组及本机 ARP；裁决只识别种类。
    [[nodiscard]] static Filter buildFilter(const PcapParams &params)
    {
        return ARPPacket::matchARP({}, {}, {}, {}, {}, {}, params.hostIPv4) |
               UDPDatagram::matchUDP({}, {}, params.deviceIPv4, params.hostIPv4,
                                     params.devicePort, params.hostPort);
    }

    [[nodiscard]] bool connLoop(std::shared_ptr<PcapConn> conn,
        std::function<void(std::shared_ptr<PcapConn>, const QByteArray &, RxTime)> sink) override
    {
        std::function<void(const QByteArray &, RxTime)> feed = [&conn, &sink](const QByteArray &frame, RxTime at) { sink(conn, frame, at); };
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
        (*reinterpret_cast<std::function<void(const QByteArray &, RxTime)> *>(user))(view,
            std::chrono::system_clock::from_time_t(header->ts.tv_sec) +
            std::chrono::microseconds{header->ts.tv_usec});
    }

    [[nodiscard]] RXRuling onRX(const QByteArray &raw, const RxTime at, std::shared_ptr<PcapConn> conn) override
    {
        switch (kindOf(raw)) // conn 因果必非空（句柄→loop→dispatch），本口自解不判
        {
        case FrameKind::UDPDatagram:
            return {UDPDatagram(raw).bodyView(), RXRuling::Decode, at};
        case FrameKind::ARPRequest:
            return {ARPReply(ARPRequest(raw), conn->params.host.macAddress), RXRuling::Reject};
        case FrameKind::ARPReply:
        {
            const ARPReply reply(raw);
            if (reply.senderIP() != conn->params.deviceIPv4)
                return {};
            const MAC mac = reply.senderMAC();
            // 收包只提取 MAC；学习与补发在所属线程一次完成，原连接随队列项保活。
            QMetaObject::invokeMethod(this, [this, conn, mac] {
                conn->deviceMAC = mac;
                for (const QByteArray &payload : conn->pending)
                    (void)connSend(conn, UDPDatagram(mac, conn->params.host.macAddress,
                        conn->params.hostIPv4, conn->params.deviceIPv4,
                        conn->params.hostPort, conn->params.devicePort, payload));
                conn->pending.clear();
            }, Qt::QueuedConnection);
            return {};
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
        if (conn->deviceMAC.isNull())
        {
            conn->pending.push_back(payload);
            Q_EMIT logMessage("对端 MAC 未知，报文已暂存，正在探测");
            return {ARPRequest(conn->params.host.macAddress, conn->params.hostIPv4, conn->params.deviceIPv4), TXRuling::Encode};
        }
        return {UDPDatagram(conn->deviceMAC, conn->params.host.macAddress, conn->params.hostIPv4, conn->params.deviceIPv4,
                            conn->params.hostPort, conn->params.devicePort, payload),
                TXRuling::Encode};
    }
};

