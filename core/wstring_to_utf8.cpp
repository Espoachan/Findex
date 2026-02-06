#include "wstring_to_utf8.hpp"

std::string wstringToUtf8(const WCHAR* p_file_name, int char_count) {
    if (char_count == 0) return "";

    int buffer_size = WideCharToMultiByte(CP_UTF8, 0, p_file_name, char_count, nullptr, 0, nullptr, nullptr);

    std::string utf8_string(buffer_size, 0);

    WideCharToMultiByte(CP_UTF8, 0, p_file_name, char_count, &utf8_string[0], buffer_size, nullptr, nullptr);

    return utf8_string;
}