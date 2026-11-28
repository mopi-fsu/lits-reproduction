#include "lits/core/index.hpp"

#include <iostream>
#include <string>
#include <vector>

// Small tour of the single-threaded LITS reproduction index. This is a usage
// example, not a benchmark: the index uses a portable subtrie stand-in for HOT and
// fixed structure rules in place of PMSS, so its timings are not LITS results.
int main() {
    lits::LitsIndex index;

    std::vector<lits::Record> records;
    for (int i = 0; i < 1000; ++i) {
        std::string key = "user/" + std::to_string(100000 + i * 7);
        records.push_back({key, static_cast<lits::Value>(i)});
    }
    // Keys must be sorted and unique; these are fixed-width, so they are.
    index.bulkload(records);
    std::cout << "bulkloaded " << index.size() << " keys, root = "
              << lits::to_string(index.root_kind()) << "\n";

    if (auto v = index.lookup("user/100007")) {
        std::cout << "lookup user/100007 -> " << *v << "\n";
    }
    std::cout << "lookup of a missing key -> "
              << (index.lookup("user/nope") ? "found" : "miss") << "\n";

    index.insert("user/100008", 4242);
    index.update("user/100007", 7);
    index.erase("user/100014");

    std::cout << "scan from user/100005, 4 records:\n";
    for (const auto& r : index.scan("user/100005", 4)) {
        std::cout << "  " << r.key << " = " << r.value << "\n";
    }
    return 0;
}
