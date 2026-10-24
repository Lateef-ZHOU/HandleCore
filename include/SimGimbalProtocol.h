#pragma once
#include <cstring>
#include <optional>
#include <type_traits>
#include <QByteArray>
#include "GimbalMotion.h"

// 进程内命令与回报：按本机 POD 布局复制，使用方与 Handle 必须使用同一份定义。
// 不承诺网络字节序或跨 ABI 兼容；不作为 Windows/Linux 之间的 UDP 传输协议。
namespace SimGimbalWire
{
    struct Command
    {
        enum Kind : int
        {
            GoTo,     // 去目标位形（pan / tilt）
            Stop,     // 停：目标置空，原地不动
            SetSpeed, // 改两轴限速（limits）
        };
        Kind kind;
        double pan;
        double tilt;
        MotionLimits limits;
    };
    struct Report
    {
        double pan;
        double tilt;
    };
    static_assert(std::is_trivially_copyable_v<Command> && std::is_trivially_copyable_v<Report>);

    template <typename T>
        requires (std::is_same_v<T, Command> || std::is_same_v<T, Report>)
    [[nodiscard]] inline QByteArray encode(const T &v)
    {
        return QByteArray(reinterpret_cast<const char *>(&v), sizeof v);
    }
    template <typename T>
        requires (std::is_same_v<T, Command> || std::is_same_v<T, Report>)
    [[nodiscard]] inline std::optional<T> decode(const QByteArray &b)
    {
        if (b.size() != static_cast<qsizetype>(sizeof(T)))
            return std::nullopt;
        T v;
        std::memcpy(&v, b.constData(), sizeof v);
        return v;
    }
} // namespace SimGimbalWire
