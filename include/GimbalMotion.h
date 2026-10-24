#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

struct MotionLimits
{
    double panSpeedDps;
    double tiltSpeedDps;

    [[nodiscard]] bool isValid() const noexcept
    {
        return std::isfinite(panSpeedDps) && panSpeedDps >= 0.0 &&
               std::isfinite(tiltSpeedDps) && tiltSpeedDps >= 0.0;
    }
};

// 两轴匀速运动，角度为度、速度为度/秒；不包含安装朝向、设备行程或 UI 类型。
// 两轴都走圆周最短路，正负 180 度的等长路径统一取正向。零限速保持该轴位置。
class GimbalMotion
{
public:
    struct Pose
    {
        double pan;
        double tilt;
        [[nodiscard]] bool isFinite() const noexcept { return std::isfinite(pan) && std::isfinite(tilt); }
    };

    GimbalMotion(const Pose start, const MotionLimits limits) : pose_(normalized(start)), limits_(limits)
    {
        if (!limits.isValid())
            throw std::invalid_argument("Gimbal speed must be finite and nonnegative");
    }

    void aim(const Pose target) { target_ = normalized(target); }
    void halt() { target_.reset(); }
    void setLimits(const MotionLimits limits)
    {
        if (!limits.isValid())
            throw std::invalid_argument("Gimbal speed must be finite and nonnegative");
        limits_ = limits;
    }

    // 无目标则原地保持；到点不过冲，不增加加减速段。非法输入在修改状态前拒绝。
    void step(const double dtSec)
    {
        if (!std::isfinite(dtSec) || dtSec < 0.0)
            throw std::invalid_argument("Gimbal time step must be finite and nonnegative");
        if (!target_)
            return;
        pose_ = normalized({stepAxis(pose_.pan, target_->pan, limits_.panSpeedDps * dtSec),
                            stepAxis(pose_.tilt, target_->tilt, limits_.tiltSpeedDps * dtSec)});
    }

    [[nodiscard]] const Pose &pose() const { return pose_; }
    [[nodiscard]] const std::optional<Pose> &target() const { return target_; }
    [[nodiscard]] const MotionLimits &limits() const { return limits_; }

private:
    [[nodiscard]] static double wrap(const double value, const double lower)
    {
        if (value >= lower && value < lower + 360.0)
            return value;
        const double phase = std::fmod(value - lower, 360.0);
        return lower + (phase < 0.0 ? phase + 360.0 : phase);
    }
    [[nodiscard]] static Pose normalized(const Pose pose)
    {
        if (!pose.isFinite())
            throw std::invalid_argument("Gimbal angles must be finite");
        return {wrap(pose.pan, 0.0), wrap(pose.tilt, -180.0)};
    }
    [[nodiscard]] static double shortest(double delta)
    {
        delta = std::fmod(delta, 360.0);
        if (delta > 180.0)
            delta -= 360.0;
        else if (delta <= -180.0)
            delta += 360.0;
        return delta;
    }
    [[nodiscard]] static double stepAxis(const double pos, const double target, const double stride)
    {
        return pos + std::clamp(shortest(target - pos), -stride, stride);
    }

    Pose pose_;
    std::optional<Pose> target_;
    MotionLimits limits_;
};
