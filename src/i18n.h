#pragma once

namespace pt {

enum class Lang { En, Ru };
inline Lang g_lang = Lang::En;

// Source files are UTF-8 (/utf-8), so plain literals carry Cyrillic fine.
inline const char* Tr(const char* en, const char* ru) { return g_lang == Lang::Ru ? ru : en; }

struct Str {
    const char* en;
    const char* ru;
    const char* get() const { return Tr(en, ru); }
};

}  // namespace pt
