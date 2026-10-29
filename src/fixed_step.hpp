#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace gm {
class FixedStep {
  public:
    explicit FixedStep(double hz = 60, uint32_t maxSteps = 8) : step_(1 / hz), maxSteps_(maxSteps) {
        if (!std::isfinite(hz) || hz < 1 || hz > 1000 || !maxSteps || maxSteps > 1000)
            throw std::runtime_error("Invalid fixed step configuration");
    }
    template <class F> uint32_t advance(double elapsed, F simulate) {
        if (!std::isfinite(elapsed) || elapsed < 0)
            throw std::runtime_error("Invalid elapsed time");
        // Bound catch-up work after a breakpoint, minimize or a slow frame.
        const double accepted = std::min(elapsed, step_ * maxSteps_);
        dropped_ += elapsed - accepted;
        accumulator_ += accepted;
        uint32_t steps = 0;
        while (accumulator_ + 1e-9 >= step_ && steps < maxSteps_) {
            simulate(float(step_));
            accumulator_ = std::max(0., accumulator_ - step_);
            ++steps;
        }
        return steps;
    }
    void reset() {
        accumulator_ = dropped_ = 0;
    }
    double alpha() const {
        return accumulator_ / step_;
    }
    double droppedSeconds() const {
        return dropped_;
    }
    float delta() const {
        return float(step_);
    }

  private:
    double step_, accumulator_ = 0, dropped_ = 0;
    uint32_t maxSteps_;
};
} // namespace gm
