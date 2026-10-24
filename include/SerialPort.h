#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <libserialport.h>

// 常用串口档位；实际是否支持由串口库及驱动校验。
enum class BaudRate : int
{
    B1200 = 1200,
    B2400 = 2400,
    B4800 = 4800,
    B9600 = 9600,
    B19200 = 19200,
    B38400 = 38400,
    B57600 = 57600,
    B115200 = 115200,
};

enum class DataBits : int
{
    D5 = 5,
    D6 = 6,
    D7 = 7,
    D8 = 8,
};

enum class StopBits : int
{
    One = 1,
    Two = 2, // libserialport 只收 1/2（无 1.5）——档位即枚举全集
};

[[nodiscard]] inline QList<BaudRate> availableBaudRates()
{
    return {BaudRate::B1200, BaudRate::B2400, BaudRate::B4800, BaudRate::B9600,
            BaudRate::B19200, BaudRate::B38400, BaudRate::B57600, BaudRate::B115200};
}

[[nodiscard]] inline QList<DataBits> availableDataBits()
{
    return {DataBits::D5, DataBits::D6, DataBits::D7, DataBits::D8};
}

[[nodiscard]] inline QList<StopBits> availableStopBits() { return {StopBits::One, StopBits::Two}; }

[[nodiscard]] inline QList<sp_parity> availableParities()
{
    return {SP_PARITY_NONE, SP_PARITY_EVEN, SP_PARITY_ODD, SP_PARITY_MARK, SP_PARITY_SPACE};
}

// ------------------------------------------------------------
// 常用档位的显示文本；界面可以使用这些选项，连接波特率不受此列表限制。
// ------------------------------------------------------------
[[nodiscard]] inline QString toString(const BaudRate v) { return QString::number(qToUnderlying(v)); }
[[nodiscard]] inline QString toString(const DataBits v) { return QString::number(qToUnderlying(v)); }
[[nodiscard]] inline QString toString(const StopBits v) { return QString::number(qToUnderlying(v)); }
[[nodiscard]] inline QString toString(const sp_parity v)
{
    switch (v)
    {
    case SP_PARITY_NONE: return QStringLiteral("None");
    case SP_PARITY_EVEN: return QStringLiteral("Even");
    case SP_PARITY_ODD: return QStringLiteral("Odd");
    case SP_PARITY_MARK: return QStringLiteral("Mark");
    case SP_PARITY_SPACE: return QStringLiteral("Space");
    default: return {};
    }
}

// 一张档位表 → 一张字符串表。界面与日志读的是**同一份**（下拉第 i 项与日志里那个名字
// 必然一致，因为就是同一个 QStringList 的同一个下标）。
template <typename T>
[[nodiscard]] QStringList optionTexts(const QList<T> &options)
{
    QStringList texts;
    texts.reserve(options.size());
    for (const T v : options)
        texts << toString(v);
    return texts;
}

// ------------------------------------------------------------
// SerialPortInfo — 串口设备信息（对仗 Pcap 侧 NetworkInterfaceInfo：名字为身份键 + 描述作 tooltip）
// ------------------------------------------------------------
class SerialPortInfo
{
public:
    QString portName;    // COM 名称或 /dev 路径；sp_get_port_by_name 的身份键
    QString description; // 设备描述（如 "USB-SERIAL CH340"）

    bool operator==(const SerialPortInfo &o) const { return portName == o.portName; }
};

// 查询当下可用端口；是否缓存列表由调用者决定，连接也可直接使用名称/路径。
[[nodiscard]] inline QList<SerialPortInfo> availablePorts()
{
    QList<SerialPortInfo> infos;
    sp_port **ports = nullptr;
    if (sp_list_ports(&ports) != SP_OK)
        return infos;
    for (int i = 0; ports[i] != nullptr; ++i)
    {
        infos.append({QString::fromLocal8Bit(sp_get_port_name(ports[i])),
                      QString::fromLocal8Bit(sp_get_port_description(ports[i]))});
    }
    sp_free_port_list(ports);
    return infos;
}

// 接受的参数由 Handle 保存供重连；界面草稿不在本库维护。
struct SerialParams
{
    SerialPortInfo portInfo;
    int baudRate = 9600;
    DataBits dataBits = DataBits::D8;
    StopBits stopBits = StopBits::One;
    sp_parity parity = SP_PARITY_NONE;
    sp_flowcontrol flowControl = SP_FLOWCONTROL_NONE;
    unsigned writeTimeoutMs = 500;

    bool operator==(const SerialParams &) const = default;
};
