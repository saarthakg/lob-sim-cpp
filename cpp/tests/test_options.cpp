#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include "../include/strategy/run_setup.hpp"
#include "../include/util/options.hpp"

TEST_CASE("Options reject typos, missing values and malformed numbers", "[options]") {
    const char* ok[] = {"lob_sim", "--replay", "x", "--order-size", "2.5", "--quiet"};
    const Options o = Options::parse(6, ok);
    REQUIRE(o.num("--order-size", 0) == 2.5);
    REQUIRE(o.has("--quiet"));
    REQUIRE(o.num("--gamma", 7) == 7);

    const char* typo[] = {"lob_sim", "--order-siez", "1"};
    REQUIRE_THROWS_WITH(Options::parse(3, typo), "unknown option --order-siez (did you mean --order-size?)");
    const char* missing[] = {"lob_sim", "--gamma"};
    REQUIRE_THROWS(Options::parse(2, missing));
    const char* junk[] = {"lob_sim", "--gamma", "1x"};
    REQUIRE_THROWS(Options::parse(3, junk).num("--gamma", 0));
    Options f;
    REQUIRE_THROWS(f.set("--quiet", "yes"));   // a flag takes no value
    REQUIRE_THROWS(time_of_day_ns("25:99:00"));
    REQUIRE(time_of_day_ns("09:35:00") == 34'500'000'000'000ULL);
}

TEST_CASE("Run setup: option values are checked and the gateway leaves room for a full ladder", "[options]") {
    Instrument i;
    i.tick_size = 0.01; i.lot_size = 1;
    Options o;
    o.set("--order-size", "100");
    o.set("--max-position", "500");
    REQUIRE(make_run_setup(o, i, "naive", "").cfg.max_position == 600);
    REQUIRE(make_run_setup(o, i, "ladder", "").cfg.max_position == 800);   // ladder defaults to 3 levels
    o.set("--levels", "2");
    REQUIRE(make_run_setup(o, i, "ladder", "").cfg.max_position == 700);
    o.set("--maker-fee-per-unit", "-0.003");
    const auto fees = make_run_setup(o, i, "naive", "").cfg.exchange.fees;
    REQUIRE(fees.maker == 0.0);              // replaces the bp default for that side only
    REQUIRE(fees.taker == 0.00055);
    o.set("--queue", "pwoer");
    REQUIRE_THROWS(make_run_setup(o, i, "naive", ""));
    REQUIRE_THROWS(make_strategy("nope", Options{}, i, 1, 10, ""));
}
