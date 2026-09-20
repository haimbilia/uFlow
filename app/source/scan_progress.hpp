#pragma once
#include <cstddef>
#include <functional>
#include <string>
struct ScanProgress {
    std::string stage, path;
    std::size_t games=0, completed=0, total=0;
};
using ScanObserver=std::function<void(const ScanProgress &)>;
