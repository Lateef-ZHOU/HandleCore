#pragma once
#include <memory>
#include <QList>
#include <QString>
#include <pcap.h>
#include "PcapProtocol.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <iphlpapi.h>
#elif defined(__linux__)
#include <net/if_arp.h>
#include <netpacket/packet.h>
#endif

struct NetworkInterfaceInfo
{
    QString friendlyName;
    QString description;
    QString pcapDeviceName;
    MAC macAddress;
    bool operator==(const NetworkInterfaceInfo &other) const { return pcapDeviceName == other.pcapDeviceName; }
};

// 返回 pcap 实际识别的以太网接口及六字节 MAC；不缓存 UI 下标，不打开网卡。
// Windows 补充系统友好名称；Linux 从 pcap 的 AF_PACKET 地址取得 MAC。
[[nodiscard]] inline QList<NetworkInterfaceInfo> availableInterfaces()
{
    pcap_if_t *raw = nullptr;
    char error[PCAP_ERRBUF_SIZE]{};
    if (pcap_findalldevs(&raw, error) != 0) return {};
    const std::unique_ptr<pcap_if_t, decltype(&pcap_freealldevs)> devices(raw, pcap_freealldevs);
    QList<NetworkInterfaceInfo> result;
#ifdef _WIN32
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW) return result;
    for (;;)
    {
        const auto buffer = std::make_unique<BYTE[]>(size);
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.get());
        const ULONG status = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &size);
        if (status == ERROR_BUFFER_OVERFLOW) continue;
        if (status != NO_ERROR) return result;
        for (const auto *adapter = adapters; adapter; adapter = adapter->Next)
        {
            if ((adapter->IfType != IF_TYPE_ETHERNET_CSMACD && adapter->IfType != IF_TYPE_IEEE80211) ||
                adapter->PhysicalAddressLength != MAC::Size) continue;
            const auto name = QStringLiteral("\\Device\\NPF_") + QString::fromLatin1(adapter->AdapterName);
            for (const auto *device = devices.get(); device; device = device->next)
            {
                if (QString::fromLocal8Bit(device->name).compare(name, Qt::CaseInsensitive) != 0) continue;
                result.append({QString::fromWCharArray(adapter->FriendlyName),
                    QString::fromWCharArray(adapter->Description), QString::fromLocal8Bit(device->name),
                    MAC(QByteArray(reinterpret_cast<const char *>(adapter->PhysicalAddress), MAC::Size))});
                break;
            }
        }
        return result;
    }
#elif defined(__linux__)
    for (const auto *device = devices.get(); device; device = device->next)
    {
        for (const auto *address = device->addresses; address; address = address->next)
        {
            if (!address->addr || address->addr->sa_family != AF_PACKET) continue;
            const auto *link = reinterpret_cast<const sockaddr_ll *>(address->addr);
            if (link->sll_hatype != ARPHRD_ETHER || link->sll_halen != MAC::Size) continue;
            const auto name = QString::fromLocal8Bit(device->name);
            result.append({name, device->description ? QString::fromLocal8Bit(device->description) : name,
                name, MAC(QByteArray(reinterpret_cast<const char *>(link->sll_addr), MAC::Size))});
            break;
        }
    }
#endif
    return result;
}
