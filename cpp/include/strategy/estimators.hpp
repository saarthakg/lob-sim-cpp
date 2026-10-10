#pragma once
#include "../core/types.hpp"
#include <cmath>
#include <deque>
#include <vector>

// Rolling online estimators used by the GLFT market maker. Every estimate is
// strictly causal: it uses only data the strategy has already seen.

// σ of mid-price changes, in ticks / sqrt(second), from fixed-interval samples.
class VolatilityEstimator {
public:
    VolatilityEstimator(double step_s, size_t window_steps)
        : step_s_(step_s), window_(window_steps) {}

    void add_mid(double mid_ticks) {
        if (has_last_) {
            double d = mid_ticks - last_;
            diffs_.push_back(d);
            sum_ += d;
            sum2_ += d * d;
            if (diffs_.size() > window_) {
                double o = diffs_.front();
                diffs_.pop_front();
                sum_ -= o;
                sum2_ -= o * o;
            }
        }
        last_ = mid_ticks;
        has_last_ = true;
    }
    size_t samples() const { return diffs_.size(); }
    double sigma() const {
        size_t n = diffs_.size();
        if (n < 2) return 0.0;
        double mean = sum_ / n;
        double var  = std::max(sum2_ / n - mean * mean, 0.0);
        return std::sqrt(var / step_s_);
    }

private:
    double step_s_;
    size_t window_;
    std::deque<double> diffs_;
    double sum_ = 0.0, sum2_ = 0.0;
    double last_ = 0.0;
    bool has_last_ = false;
};

// Fill intensity λ(δ) = A·exp(−k·δ) of a resting order δ ticks from the mid,
// measured from public trades (Avellaneda–Stoikov / GLFT calibration, after
// the hftbacktest GLFT tutorial). For each fixed step Δt we record, per side,
// how far from the step's starting mid the furthest trade printed. A
// hypothetical order at depth δ on that side would have been reached in that
// step iff max_depth ≥ δ. λ(δ) = P(reached in a step) / Δt, pooled over both
// sides, then ln λ is fit linearly in δ by count-weighted least squares (the
// weighting keeps sparse, noisy deep buckets from dominating the fit).
class IntensityEstimator {
public:
    IntensityEstimator(double step_s, size_t window_steps, int max_depth_ticks)
        : step_s_(step_s), window_(window_steps), max_depth_(max_depth_ticks),
          counts_(max_depth_ticks, 0) {}

    // Start a new step anchored at this mid.
    void begin_step(double mid_ticks) {
        if (in_step_) finish_step();
        step_mid_ = mid_ticks;
        cur_buy_ = cur_sell_ = -1.0;
        in_step_ = true;
    }
    void on_trade(bool buyer_aggressor, double price_ticks) {
        if (!in_step_) return;
        if (buyer_aggressor) cur_buy_  = std::max(cur_buy_,  price_ticks - step_mid_);
        else                 cur_sell_ = std::max(cur_sell_, step_mid_ - price_ticks);
    }
    size_t steps() const { return steps_.size(); }

    // Returns false if there isn't enough signal to fit.
    bool fit(double& A, double& k) const {
        if (steps_.size() < 100) return false;
        const double denom = 2.0 * static_cast<double>(steps_.size()) * step_s_;
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        int used = 0;
        for (int j = 0; j < max_depth_; ++j) {
            if (counts_[j] <= 0) continue;
            double x = depth_of(j);
            double y = std::log(static_cast<double>(counts_[j]) / denom);
            double w = static_cast<double>(counts_[j]);
            sw += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y;
            ++used;
        }
        if (used < 2) return false;
        double var = sxx * sw - sx * sx;
        if (var <= 0) return false;
        double slope = (sxy * sw - sx * sy) / var;
        double icpt  = (sy - slope * sx) / sw;
        if (!(slope < 0)) return false;
        k = -slope;
        A = std::exp(icpt);
        return std::isfinite(A) && std::isfinite(k);
    }

    // Empirical λ at bucket j (per second, per side) — for diagnostics.
    double lambda_at(int j) const {
        if (steps_.empty()) return 0.0;
        return counts_[j] / (2.0 * steps_.size() * step_s_);
    }
    static double depth_of(int j) { return 0.5 + j; }

private:
    struct Step { float buy, sell; };
    double step_s_;
    size_t window_;
    int    max_depth_;
    std::vector<int64_t> counts_;   // counts_[j]: side-steps with max_depth >= depth_of(j)
    std::deque<Step> steps_;
    double step_mid_ = 0.0, cur_buy_ = -1.0, cur_sell_ = -1.0;
    bool in_step_ = false;

    void bump(double d, int sign) {
        if (d < 0) return;
        for (int j = 0; j < max_depth_ && depth_of(j) <= d + 1e-9; ++j) counts_[j] += sign;
    }
    void finish_step() {
        steps_.push_back({static_cast<float>(cur_buy_), static_cast<float>(cur_sell_)});
        bump(cur_buy_, +1);
        bump(cur_sell_, +1);
        if (steps_.size() > window_) {
            Step o = steps_.front();
            steps_.pop_front();
            bump(o.buy, -1);
            bump(o.sell, -1);
        }
    }
};
