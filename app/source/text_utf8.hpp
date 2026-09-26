#pragma once

// UTF-8 helpers for UI text. Titles come from meta.xml as UTF-8 and may be
// Japanese, Chinese or Korean; nothing here may split a multi-byte character.
#include <cstddef>
#include <cstdint>
#include <string>

namespace Utf8 {
// Decodes one code point at text[i] and advances i. Malformed bytes decode to
// U+FFFD and advance by one, so callers always make progress.
inline std::uint32_t Next(const std::string &text, std::size_t &i) {
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char lead = byte(i);
    if (lead < 0x80) { ++i; return lead; }
    const int length = lead < 0xC2 ? 0 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : lead < 0xF5 ? 4 : 0;
    if (!length || i + length > text.size()) { ++i; return 0xFFFD; }
    std::uint32_t cp = lead & (0x7F >> length);
    for (int k = 1; k < length; ++k) {
        if ((byte(i + k) & 0xC0) != 0x80) { ++i; return 0xFFFD; }
        cp = cp << 6 | (byte(i + k) & 0x3F);
    }
    static constexpr std::uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < minimum[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++i; return 0xFFFD; }
    i += length;
    return cp;
}

inline bool HasNonAscii(const std::string &text) {
    for (unsigned char c : text) if (c >= 0x80) return true;
    return false;
}

// meta.xml names use line breaks for two-line titles; the UI shows one line.
inline std::string OneLine(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (c < 32 || c == 127) c = ' ';
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Keeps at most maximum code points, ending with "..." when shortened.
inline std::string Ellipsize(const std::string &text, std::size_t maximum) {
    const std::string line = OneLine(text);
    std::size_t count = 0, cut3 = line.size(), cut = line.size();
    for (std::size_t i = 0; i < line.size(); ++count) {
        if (count + 3 == maximum) cut3 = i;
        if (count == maximum) { cut = i; break; }
        Next(line, i);
    }
    if (cut == line.size()) return line;
    if (maximum <= 3) return line.substr(0, cut);
    return line.substr(0, cut3) + "...";
}
} // namespace Utf8
