#pragma once
#include <cstring>
#include <optional>
#include <type_traits>
#include <QByteArray>

// 进程内命令使用本机 POD 布局；结果以同一 requestId 开头，载荷由应用解释。
// 请求号用于 Collector 拒绝取消/重试前的结果，不是跨 ABI 的网络协议。
namespace SimCardWire
{
    struct Command
    {
        enum Kind : int { Accumulate, Stop };
        Kind kind;
        int accumSec;
        quint64 requestId;
    };
    static_assert(std::is_trivially_copyable_v<Command>);
    inline constexpr qsizetype headerBytes = sizeof(quint64);

    template <typename T>
        requires (std::is_same_v<T, Command> || std::is_same_v<T, quint64>)
    [[nodiscard]] inline QByteArray encode(const T &v)
    { return QByteArray(reinterpret_cast<const char *>(&v), sizeof v); }

    template <typename T>
        requires (std::is_same_v<T, Command> || std::is_same_v<T, quint64>)
    [[nodiscard]] inline std::optional<T> decode(const QByteArray &b)
    {
        if (b.size() != static_cast<qsizetype>(sizeof(T))) return std::nullopt;
        T v;
        std::memcpy(&v, b.constData(), sizeof v);
        return v;
    }
}
