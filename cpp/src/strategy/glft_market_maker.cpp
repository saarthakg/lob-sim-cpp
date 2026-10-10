#include "../../include/strategy/glft_market_maker.hpp"
#include "../../include/strategy/quote_manager.hpp"

#include <algorithm>
#include <cmath>

GlftMarketMaker::GlftMarketMaker() : GlftMarketMaker(Config{}) {}

GlftMarketMaker::GlftMarketMaker(Config cfg)
    : cfg_(std::move(cfg)),
      vol_(static_cast<double>(cfg_.quote_interval_us) / 1e6, cfg_.window_steps),
      inten_(static_cast<double>(cfg_.quote_interval_us) / 1e6, cfg_.window_steps, cfg_.max_depth_ticks),
      guard_(cfg_.depletion_pull) {}

GlftMarketMaker::~GlftMarketMaker() {
    if (log_) std::fclose(log_);
}

void GlftMarketMaker::glft_coefficients(double gamma, double A, double k, double sigma,
                                        double& half_spread, double& skew) {
    const double xi = gamma;  // model A: ξ = γ; Δ = 1 (q measured in order-size units)
    const double c1 = (1.0 / xi) * std::log(1.0 + xi / k);
    const double c2 = std::sqrt(gamma / (2.0 * A * k) * std::pow(1.0 + xi / k, k / xi + 1.0));
    half_spread = c1 + 0.5 * sigma * c2;
    skew        = sigma * c2;
}

void GlftMarketMaker::on_start(StrategyContext& ctx) {
    (void)ctx;
    if (!cfg_.log_path.empty()) {
        log_ = std::fopen(cfg_.log_path.c_str(), "w");
        if (log_) std::fprintf(log_, "ts,A,k,sigma,half_spread,skew,position,lambda_0,lambda_1,lambda_2\n");
    }
}

void GlftMarketMaker::on_trade(StrategyContext& ctx, const TradePrint& tr) {
    inten_.on_trade(tr.aggressor == Side::Bid, static_cast<double>(tr.price));
    guard_.on_trade(ctx, tr);
}

void GlftMarketMaker::on_book(StrategyContext& ctx) {
    guard_.on_book();
    if (cfg_.quote_on_book && calibrated_ && start_ts_ != 0 && ctx.now() - start_ts_ >= cfg_.warmup_us) quote(ctx);
}

void GlftMarketMaker::on_timer(StrategyContext& ctx) {
    const OrderBook& book = ctx.book();
    if (book.empty()) return;
    const double mid = book.mid_price();
    if (start_ts_ == 0) {
        start_ts_   = ctx.now();
        next_calib_ = ctx.now() + cfg_.calib_interval_us;
    }
    vol_.add_mid(mid);
    inten_.begin_step(mid);

    if (ctx.now() >= next_calib_) {
        next_calib_ += cfg_.calib_interval_us;
        calibrate(ctx);
    }
    if (!cfg_.quote_on_book && calibrated_ && ctx.now() - start_ts_ >= cfg_.warmup_us) quote(ctx);
}

void GlftMarketMaker::calibrate(StrategyContext& ctx) {
    double A = 0, k = 0;
    if (!inten_.fit(A, k)) return;
    double sigma = vol_.sigma();
    if (!(sigma > 0)) return;
    A_ = A; k_ = k; sigma_ = sigma;
    glft_coefficients(cfg_.gamma, A_, k_, sigma_, half_, skew_);
    if (!std::isfinite(half_) || !std::isfinite(skew_)) { calibrated_ = false; return; }
    calibrated_ = true;
    if (log_) {
        std::fprintf(log_, "%lld,%.6f,%.6f,%.6f,%.4f,%.4f,%lld,%.6f,%.6f,%.6f\n",
                     static_cast<long long>(ctx.now()), A_, k_, sigma_, half_, skew_,
                     static_cast<long long>(ctx.position()), inten_.lambda_at(0),
                     inten_.lambda_at(1), inten_.lambda_at(2));
    }
}

void GlftMarketMaker::quote(StrategyContext& ctx) {
    const OrderBook& book = ctx.book();
    const Price best_bid = book.best_bid()->price;
    const Price best_ask = book.best_ask()->price;
    const double mid = book.mid_price();

    const double q        = static_cast<double>(ctx.position()) / static_cast<double>(cfg_.order_qty);
    const double fee_tick = cfg_.include_fee ? ctx.maker_fee() * mid : 0.0;  // fee per unit, in ticks

    double alpha = 0.0;
    if (cfg_.alpha_micro != 0.0) alpha += cfg_.alpha_micro * (book.weighted_mid() - mid);
    if (cfg_.alpha_obi != 0.0) {
        Price band = std::max<Price>(1, static_cast<Price>(std::llround(mid * cfg_.obi_band_bps * 1e-4)));
        alpha += cfg_.alpha_obi * book.depth_imbalance(band);
    }
    const double fair = mid + alpha;

    const double hs = std::max(half_ * cfg_.half_spread_mult, cfg_.min_half_spread_ticks);
    const double sk = skew_ * cfg_.skew_mult;
    const double bid_depth = hs + sk * q + fee_tick;
    const double ask_depth = hs - sk * q + fee_tick;

    Price bid0 = static_cast<Price>(std::floor(fair - bid_depth));
    Price ask0 = static_cast<Price>(std::ceil(fair + ask_depth));
    // Post-only: never cross the (local view of the) opposite touch.
    bid0 = std::min(bid0, best_ask - 1);
    ask0 = std::max(ask0, best_bid + 1);
    if (bid0 >= ask0) {
        // Only possible with a rebate larger than the half spread inside a
        // wide book; split around fair.
        Price f = static_cast<Price>(std::floor(fair));
        bid0 = std::min({bid0, f, best_ask - 1});
        ask0 = std::max({ask0, f + 1, best_bid + 1});
    }

    const Price step = std::max<Price>(1, static_cast<Price>(std::llround(hs)));
    const Qty pos = ctx.position();
    std::vector<Price> bids, asks;
    for (int i = 0; i < cfg_.levels; ++i) {
        const Price b = bid0 - i * step, a = ask0 + i * step;
        if (pos + cfg_.order_qty <= cfg_.max_position && b > 0 && !guard_.blocked(Side::Bid, b)) bids.push_back(b);
        if (pos - cfg_.order_qty >= -cfg_.max_position && !guard_.blocked(Side::Ask, a)) asks.push_back(a);
    }
    sync_quotes(ctx, Side::Bid, bids, cfg_.order_qty);
    sync_quotes(ctx, Side::Ask, asks, cfg_.order_qty);
}
