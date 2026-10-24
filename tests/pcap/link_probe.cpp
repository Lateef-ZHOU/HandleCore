#include <PcapHandle.h>
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

void require(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = false;
    const auto host = IPv4::fromString("192.0.2.1", ok);
    const auto peer = IPv4::fromString("192.0.2.2", ok);
    const auto hostMac = MAC::fromString("02:00:00:00:00:01", ok);
    const auto peerMac = MAC::fromString("02:00:00:00:00:02", ok);
    const Filter filter = ARPPacket::matchARP({}, {}, {}, {}, {}, {}, host) |
        UDPDatagram::matchUDP({}, {}, peer, host, 5678, 1234);
    pcap_t *dead = pcap_open_dead(DLT_EN10MB, 65536);
    require(dead != nullptr, "cannot create offline pcap compiler");
    bpf_program code{};
    require(pcap_compile(dead, &code, filter.toLocal8Bit().constData(), 1, PCAP_NETMASK_UNKNOWN) == 0,
            "production BPF failed compilation");
    const auto accepted = [&](const QByteArray &frame) {
        pcap_pkthdr header{}; header.caplen = header.len = static_cast<bpf_u_int32>(frame.size());
        return pcap_offline_filter(&code, &header, reinterpret_cast<const u_char *>(frame.constData())) != 0;
    };
    const UDPDatagram udp(hostMac, peerMac, peer, host, 5678, 1234, "hello");
    require(accepted(udp) && kindOf(udp) == FrameKind::UDPDatagram && UDPDatagram(udp).bodyView() == "hello",
            "UDP roundtrip/filter changed");
    require(UDPDatagram(QByteArray{}).bodyView().isEmpty() && UDPDatagram(QByteArray(41, '\0')).bodyView().isEmpty(),
            "short UDP frame read past its buffer");
    require(!accepted(UDPDatagram(hostMac, peerMac, peer, host, 5678, 1235, "wrong port")), "wrong port passed BPF");
    require(!accepted(UDPDatagram(hostMac, peerMac, host, peer, 5678, 1234, "wrong IP")), "wrong IP passed BPF");
    const ARPRequest request(peerMac, peer, host);
    require(accepted(request) && accepted(ARPReply(ARPRequest(hostMac, host, peer), peerMac)), "ARP filtering changed");
    require(!accepted(ARPRequest(hostMac, host, peer)), "foreign ARP target passed BPF");
    QByteArray vlan = udp;
    vlan.replace(12, 2, QByteArray::fromHex("8100")); vlan.insert(14, QByteArray::fromHex("00010800"));
    require(kindOf(vlan) == FrameKind::Unknown && !accepted(vlan), "VLAN unexpectedly entered fixed-format parser");
    pcap_freecode(&code); pcap_close(dead);
    const auto interfaces = availableInterfaces();
    for (const auto &nic : interfaces)
        require(!nic.pcapDeviceName.isEmpty() && nic.macAddress.size() == 6, "invalid NIC metadata");
    PcapHandle handle([](const QByteArray &, RxTime) {});
    require(!handle.send("offline"), "offline send succeeded");
    std::printf("%s; enumerated %lld Ethernet interfaces; offline ARP/UDP/BPF passed\n",
                pcap_lib_version(), static_cast<long long>(interfaces.size()));
}
