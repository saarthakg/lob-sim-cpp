#pragma once
#include "types.hpp"
#include "level.hpp"
#include <vector>

struct BookSnapshot {
    UpdateId           last_update_id;
    std::vector<Level> bids;
    std::vector<Level> asks;
};
