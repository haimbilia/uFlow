#include "../app/source/text_utf8.hpp"
#include <cassert>
#include <iostream>

int main() {
    const std::string jp = "\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80\xE3\x81\xAE\xE4\xBC\x9D\xE8\xAA\xAC"; // ゼルダの伝説
    std::size_t i = 0;
    assert(Utf8::Next(jp, i) == 0x30BC && i == 3);
    assert(Utf8::HasNonAscii(jp) && !Utf8::HasNonAscii("Zelda"));
    // Malformed input always advances and never reads past the end.
    for (const std::string &bad : {std::string("\xE3\x82"), std::string("\xC0\xAF"), std::string("\xED\xA0\x80"),
                                  std::string("\xF8\x88\x80\x80\x80"), std::string("\x80")}) {
        std::size_t at = 0;
        while (at < bad.size()) {
            const std::size_t before = at;
            assert(Utf8::Next(bad, at) == 0xFFFD && at == before + 1);
        }
    }
    // Two-line meta.xml names become one line; Japanese stays intact.
    assert(Utf8::OneLine("  Xenoblade\nChronicles X \r\n") == "Xenoblade Chronicles X");
    assert(Utf8::OneLine(jp + "\n" + jp) == jp + " " + jp);
    // Ellipsize counts characters, never splits one, and no longer blanks Japanese.
    assert(Utf8::Ellipsize(jp, 6) == jp);
    assert(Utf8::Ellipsize(jp, 5) == jp.substr(0, 6) + "...");
    assert(Utf8::Ellipsize("LAUNCHING GAME", 40) == "LAUNCHING GAME");
    assert(Utf8::Ellipsize("ABCDEFGHIJ", 6) == "ABC...");
    assert(Utf8::Ellipsize("ABCDEFGHIJ", 3) == "ABC");
    assert(Utf8::Ellipsize("", 3).empty());
    std::cout << "PASS: UTF-8 decode, malformed input, one-line names, character-safe ellipsis\n";
}
