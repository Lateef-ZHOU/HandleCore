#pragma once
#include <QObject>
#include <QMetaType>

namespace HandleCore
{
Q_NAMESPACE

// 资源事件的阶段，不是另一份运行状态。实际状态始终从资源本身读取。
// 获取失败、取消或连接丢失可以跳过阶段；AboutToRelease 不承诺资源仍然可用。
enum class ResourcePhase
{
    AboutToAcquire,
    Acquired,
    AboutToRelease,
    Released
};
Q_ENUM_NS(ResourcePhase)
}
Q_DECLARE_METATYPE(HandleCore::ResourcePhase)
