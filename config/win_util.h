#pragma once

#include <windows.h>
#include <string>

namespace winutil {
    inline bool FileExists(const std::wstring& path) {
        if (path.empty()) return false;
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    inline std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
        if (n <= 0) return std::string();
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
        return s;
    }

    inline bool IsAbsolutePath(const std::wstring& path) {
        if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
        return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
    }
}
