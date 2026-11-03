#pragma once
#include <QList>
#include <QString>
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

// 线路规格的文本表示，供 Bridge 等调用方列出设备配置。
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

// ------------------------------------------------------------
// 串口名称是连接身份键；描述供枚举展示，不参与身份比较。
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
