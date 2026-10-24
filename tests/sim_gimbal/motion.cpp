#include <GimbalMotion.h>
#include <cstdlib>
#include <iostream>
#include <limits>

static void require(const bool value, const char *what)
{
    if (!value) { std::cerr << what << '\n'; std::exit(1); }
}
static bool near(const double a, const double b) { return std::abs(a - b) < 1e-10; }
template<class F> static void rejects(F action)
{
    try { action(); } catch (const std::invalid_argument &) { return; }
    require(false, "invalid model input accepted");
}
int main()
{
    GimbalMotion motion({-10, 190}, {10, 5});
    require(near(motion.pose().pan, 350) && near(motion.pose().tilt, -170), "initial normalization");
    motion.aim({370, -190});
    motion.step(1);
    require(near(motion.pose().pan, 0) && near(motion.pose().tilt, -175), "shortest wrap and independent speed");
    motion.step(10);
    require(near(motion.pose().pan, 10) && near(motion.pose().tilt, 170), "arrival without overshoot");
    motion.aim({190, -10});
    motion.step(1);
    require(near(motion.pose().pan, 20) && near(motion.pose().tilt, 175), "both half-turn ties must be positive");
    motion.halt();
    motion.step(5);
    require(!motion.target() && near(motion.pose().pan, 20), "halt must clear target and hold position");
    motion.aim({50, -100});
    motion.setLimits({0, 10});
    motion.step(0.5);
    require(near(motion.pose().pan, 20) && near(motion.pose().tilt, -180), "zero speed and tilt boundary");
    motion.aim({15, -175});
    motion.setLimits({10, 0});
    motion.step(0.25);
    require(near(motion.pose().pan, 17.5) && near(motion.pose().tilt, -180), "target and speed replacement");
    motion.step(0);
    require(near(motion.pose().pan, 17.5), "zero duration");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    rejects([&] { GimbalMotion bad({nan, 0}, {1, 1}); });
    rejects([&] { GimbalMotion bad({0, inf}, {1, 1}); });
    rejects([&] { GimbalMotion bad({0, 0}, {-1, 1}); });
    rejects([&] { GimbalMotion bad({0, 0}, {1, inf}); });
    rejects([&] { motion.aim({0, nan}); });
    rejects([&] { motion.setLimits({nan, 1}); });
    rejects([&] { motion.setLimits({1, -1}); });
    rejects([&] { motion.step(-1); });
    rejects([&] { motion.step(inf); });
    require(near(motion.target()->pan, 15) && near(motion.limits().panSpeedDps, 10) &&
            near(motion.pose().pan, 17.5), "rejected input must preserve state");
    const double largest = std::numeric_limits<double>::max();
    motion.setLimits({largest, largest});
    motion.step(2); // stride may overflow; clamping must still reach the finite target.
    require(near(motion.pose().pan, 15) && near(motion.pose().tilt, -175), "large finite limit");
    GimbalMotion extremes({largest, -largest}, {0, 0});
    require(extremes.pose().isFinite() && extremes.pose().pan >= 0 && extremes.pose().pan < 360 &&
            extremes.pose().tilt >= -180 && extremes.pose().tilt < 180, "finite angle normalization");
    std::cout << "PASS: two-axis motion and parameter contracts (standard C++ only)\n";
}
