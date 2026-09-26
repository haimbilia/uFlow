#pragma once
#include "library.hpp"
#include "saved_file.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>

namespace LibrarySort {
inline constexpr int Count = 7;
inline const char *Name(int mode) {
    static constexpr const char *names[] = {"TITLE A-Z", "TITLE Z-A", "RECENTLY PLAYED",
        "YEAR NEWEST", "YEAR OLDEST", "PUBLISHER", "SYSTEM"};
    return names[std::clamp(mode, 0, Count-1)];
}
inline std::string Fold(std::string value) {
    for (char &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
inline std::string Key(const GameEntry &game) {
    return game.libraryId + ":" + (game.installed ? "installed:" + std::to_string(game.installedTitleId) :
        (game.absolutePath.empty() ? game.folder : game.absolutePath));
}
class History {
    std::map<std::string, uint64_t> entries_;
    uint64_t sequence_ = 0;
public:
    void Load(const std::string &path) {
        entries_.clear(); sequence_ = 0;
        auto input = SavedFile::Open(path);
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream row(line); std::string key, number, extra;
            if (!(row >> std::quoted(key) >> number) || row >> extra || key.empty() ||
                key.find_first_of("\r\n") != std::string::npos || number.empty() ||
                number.find_first_not_of("0123456789") != std::string::npos) continue;
            try {
                const auto value = std::stoull(number);
                if (!value || value == UINT64_MAX) continue;
                entries_[key] = value; sequence_ = std::max(sequence_, uint64_t(value));
            } catch (...) {}
        }
    }
    uint64_t Last(const GameEntry &game) const {
        const auto found = entries_.find(Key(game));
        return found == entries_.end() ? 0 : found->second;
    }
    bool Record(const GameEntry &game, const std::string &path) {
        const auto key = Key(game);
        if (key.find_first_of("\r\n") != std::string::npos || sequence_ >= UINT64_MAX-1) return false;
        auto next = entries_; next[key] = sequence_ + 1;
        const auto temporary = path + ".tmp";
        std::ofstream output(temporary, std::ios::trunc);
        for (const auto &[name, value] : next) output << std::quoted(name) << ' ' << value << '\n';
        output.close();
        if (!output || !SavedFile::Commit(temporary, path)) { std::remove(temporary.c_str()); return false; }
        entries_ = std::move(next); ++sequence_; return true;
    }
};
inline int Year(const std::string &text) {
    if (text.size() < 4 || text.substr(0,4).find_first_not_of("0123456789") != std::string::npos) return 0;
    const int year = std::stoi(text.substr(0,4));
    return year >= 1000 ? year : 0;
}
inline void Apply(std::vector<size_t> &visible, const std::vector<GameEntry> &games,
                  int mode, const History &history) {
    // Build keys once; comparison never touches storage or changes game indices.
    struct Row { size_t index; std::string title, group, identity; int year; uint64_t recent; };
    std::vector<Row> rows; rows.reserve(visible.size());
    for (auto index : visible) {
        const auto &g = games[index];
        rows.push_back({index, Fold(g.name), Fold(mode == 5 ? g.publisher : g.libraryId),
                        Key(g), Year(g.releaseYear), history.Last(g)});
    }
    std::stable_sort(rows.begin(), rows.end(), [mode](const Row &a, const Row &b) {
        if (mode == 2 && a.recent != b.recent) return a.recent > b.recent;
        if ((mode == 3 || mode == 4) && a.year != b.year) {
            if (!a.year || !b.year) return a.year != 0;
            return mode == 3 ? a.year > b.year : a.year < b.year;
        }
        if ((mode == 5 || mode == 6) && a.group != b.group) {
            if (a.group.empty() || b.group.empty()) return !a.group.empty();
            return a.group < b.group;
        }
        if (a.title != b.title) return mode == 1 ? a.title > b.title : a.title < b.title;
        return a.identity < b.identity;
    });
    for (size_t i = 0; i < rows.size(); ++i) visible[i] = rows[i].index;
}
} // namespace LibrarySort
