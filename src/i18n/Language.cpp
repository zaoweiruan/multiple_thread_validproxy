#include "i18n/Language.h"

#include <cctype>
#include <cstddef>

namespace i18n {
namespace {

// 归一化：去首尾空白、小写、'_' 换成 '-'。
std::string normalize(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (ch == '_') {
            out.push_back('-');
        } else {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    std::size_t begin = 0;
    while (begin < out.size() && (out[begin] == ' ' || out[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = out.size();
    while (end > begin && (out[end - 1] == ' ' || out[end - 1] == '\t' || out[end - 1] == '\r')) {
        --end;
    }
    return out.substr(begin, end - begin);
}

}  // namespace

Language defaultLanguage() {
    return Language::ZhCN;
}

bool parseLanguage(const std::string& text, Language& out) {
    const std::string key = normalize(text);
    if (key == "zh" || key == "zh-cn" || key == "zh-hans") {
        out = Language::ZhCN;
        return true;
    }
    if (key == "en" || key == "en-us" || key == "en-gb") {
        out = Language::EnUS;
        return true;
    }
    return false;
}

const char* languageCode(Language lang) {
    return lang == Language::EnUS ? "en-US" : "zh-CN";
}

}  // namespace i18n
