#include <catch2/catch_test_macros.hpp>
#include "../include/marketdata/itch_l3_extractor.hpp"
#include "../include/marketdata/event_source.hpp"
#include "../include/util/input_file.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

constexpr uint64_t T0 = 35'000'000'000'000ULL;   // 09:43:20, ns since midnight

fs::path temp_dir(const char* name) {
    fs::path d = fs::temp_directory_path() / "lobsim_tests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

std::vector<L3Record> read_all(const fs::path& p) {
    L3FileReader rd(p.string());
    std::vector<L3Record> out;
    while (const L3Record* r = rd.next()) out.push_back(*r);
    return out;
}

}  // namespace

TEST_CASE("date_to_us matches known epochs and rejects junk", "[itch]") {
    REQUIRE(date_to_us("1970-01-01") == 0);
    REQUIRE(date_to_us("2019-12-30") == 1577664000LL * 1'000'000);
    REQUIRE(date_to_us("2024-02-29") == 1709164800LL * 1'000'000);
    REQUIRE_THROWS(date_to_us("20191230"));
    REQUIRE_THROWS(date_to_us("2019-13-01"));
    REQUIRE_THROWS(date_to_us("2019-12-30x"));
}

TEST_CASE("ITCH extraction maps every order message to L3 records", "[itch][l3]") {
    const fs::path dir = temp_dir("itch_extract");
    ItchL3Extractor ex({"AAPL", "ZZZZ"}, dir.string(), "2019-12-30", date_to_us("2019-12-30"), 57'600'000'000'000ULL);
    ex.on_directory(7, "AAPL    ");
    ex.on_add(7, T0, 1, 'B', 100, 1500000);
    ex.on_add(7, T0 + 1500, 2, 'S', 80, 1500200);
    ex.on_add(9, T0, 77, 'B', 5, 1000000);                     // another stock: ignored
    ex.on_add(7, T0, 3, 'B', 10, 1);                           // $0.0001 stub: skipped
    ex.on_delete(7, T0, 3);                                    // ... and its delete is not an unknown ref
    ex.on_execute(7, T0 + 2000, 1, 40);                        // print + modify to 60
    ex.on_execute_with_price(7, T0 + 3000, 2, 30, false, 1500100);  // non-printable: book only
    ex.on_hidden_trade(7, T0 + 4000, 25, 1500050);             // midpoint, sub-penny: dropped
    ex.on_hidden_trade(7, T0 + 4000, 25, 1500000);             // hidden fill at the bid: seller-initiated
    ex.on_replace(7, T0 + 5000, 2, 4, 50, 1500300);            // delete 2, add 4 (still an ask)
    ex.on_cancel(7, T0 + 6000, 1, 60);                         // cancels the rest of 1
    ex.on_delete(7, T0 + 6000, 999);                           // unknown
    REQUIRE_FALSE(ex.done());
    ex.on_add(7, 57'600'000'000'000ULL, 50, 'B', 1, 1500000);  // 16:00:00: past the end
    REQUIRE(ex.done());
    const auto st = ex.finish();

    REQUIRE(st[0].found);
    REQUIRE_FALSE(st[1].found);
    REQUIRE(st[0].skipped_subdollar == 1);
    REQUIRE(st[0].unknown_ref == 1);
    REQUIRE(st[0].nonprintable == 1);
    REQUIRE(st[0].hidden_prints == 1);
    REQUIRE(st[0].subpenny_prints == 1);

    const auto r = read_all(dir / "2019-12-30_AAPL.l3bin");
    auto is = [&](size_t i, L3Type t, uint64_t id, int side, int64_t px, int64_t qty) {
        INFO("record " << i);
        CHECK(r[i].type == static_cast<uint8_t>(t));
        CHECK(r[i].id == id);
        CHECK(r[i].side == side);
        CHECK(r[i].price == px);
        CHECK(r[i].qty == qty);
    };
    REQUIRE(r.size() == 9);
    is(0, L3Type::Add, 1, 0, 15000, 100);
    is(1, L3Type::Add, 2, 1, 15002, 80);
    is(2, L3Type::Trade, 1, 1, 15000, 40);    // maker 1 (bid) hit by a seller
    is(3, L3Type::Modify, 1, 0, 15000, 60);
    is(4, L3Type::Modify, 2, 1, 15002, 50);
    is(5, L3Type::Trade, 0, 1, 15000, 25);    // hidden
    is(6, L3Type::Delete, 2, 1, 15002, 0);
    is(7, L3Type::Add, 4, 1, 15003, 50);
    is(8, L3Type::Delete, 1, 0, 15000, 0);
    REQUIRE(r[1].ts == date_to_us("2019-12-30") + static_cast<int64_t>((T0 + 1500) / 1000));
}

TEST_CASE("Truncated inputs throw instead of looking like a short day", "[io]") {
    const fs::path dir = temp_dir("truncated");
    const std::string path = (dir / "x.l3bin").string();
    {
        L3FileWriter w(path, "X", 0.01, 1.0);
        for (int i = 1; i <= 100000; ++i) {
            L3Record r{};
            r.ts = i; r.id = static_cast<uint64_t>(i) * 2654435761u; r.price = 100; r.qty = 1;
            r.type = static_cast<uint8_t>(L3Type::Add);
            w.write(r);
        }
        w.close();
    }
    REQUIRE(read_all(path).size() == 100000);
    REQUIRE(std::system(("gzip -k " + shell_quote(path)).c_str()) == 0);
    REQUIRE(read_all(path + ".gz").size() == 100000);   // .gz is read transparently

    fs::resize_file(path, fs::file_size(path) - 10);     // mid-record
    REQUIRE_THROWS(read_all(path));
    fs::resize_file(path, fs::file_size(path) - 38);     // whole records, but fewer than the header says
    REQUIRE_THROWS(read_all(path));
    fs::resize_file(path + ".gz", fs::file_size(path + ".gz") / 2);
    REQUIRE_THROWS(read_all(path + ".gz"));
    REQUIRE_THROWS(InputFile((dir / "missing.gz").string()));
}
