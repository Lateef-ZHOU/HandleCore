#include <HandleBase.h>

struct InvalidParams
{
#if INVALID_PARAM_CASE == 1
    InvalidParams() = delete;
    bool operator!=(const InvalidParams &) const { return false; }
#elif INVALID_PARAM_CASE == 2
    InvalidParams &operator=(const InvalidParams &) = delete;
    bool operator!=(const InvalidParams &) const { return false; }
#elif INVALID_PARAM_CASE == 3
    // 没有比较运算。
#elif INVALID_PARAM_CASE == 4
    void operator!=(const InvalidParams &) const {}
#endif
};

using RejectedHandle = Handle<InvalidParams, int>;
