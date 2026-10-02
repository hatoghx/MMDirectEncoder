#include "encoder_config.h"

#include <shlobj.h>
#include <algorithm>
#include <vector>

extern HINSTANCE g_hInst;

namespace {
    std::wstring TrimString(std::wstring s) {
        const size_t first = s.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return L"";
        const size_t last = s.find_last_not_of(L" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    std::wstring LowerString(std::wstring s) {
        for (auto& ch : s) {
            if (ch >= L'A' && ch <= L'Z') {
                ch = static_cast<wchar_t>(ch - L'A' + L'a');
            }
        }
        return s;
    }

    std::wstring GetModuleDir(HMODULE hMod) {
        std::vector<wchar_t> buf(32768);
        DWORD len = GetModuleFileNameW(hMod, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0 || len >= buf.size()) return L"";
        std::wstring p(buf.data(), len);
        size_t pos = p.find_last_of(L"\\/");
        return pos == std::wstring::npos ? L"" : p.substr(0, pos);
    }

    bool PathFileExistsDirect(const std::wstring& path) {
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }
}

std::wstring EncoderConfig::GetDefaultIniPath() {
    std::wstring dllDir = GetModuleDir(g_hInst);
    if (!dllDir.empty()) {
        std::wstring localIni = dllDir + L"\\MMDirectEncoder.ini";
        if (PathFileExistsDirect(localIni)) {
            return localIni;
        }
    }
    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        std::wstring baseDir = std::wstring(localAppData) + L"\\MMDirect Encoder";
        CreateDirectoryW(baseDir.c_str(), NULL);
        return baseDir + L"\\MMDirectEncoder.ini";
    }
    if (!dllDir.empty()) {
        return dllDir + L"\\MMDirectEncoder.ini";
    }
    return L"MMDirectEncoder.ini";
}

std::wstring EncoderConfig::GetLogDirectoryPath() {
    std::wstring dllDir = GetModuleDir(g_hInst);
    if (!dllDir.empty()) {
        std::wstring localLogs = dllDir + L"\\logs";
        if (PathFileExistsDirect(dllDir + L"\\MMDirectEncoder.ini") || PathFileExistsDirect(localLogs)) {
            CreateDirectoryW(localLogs.c_str(), NULL);
            return localLogs;
        }
    }
    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        std::wstring logDir = std::wstring(localAppData) + L"\\MMDirect Encoder\\logs";
        CreateDirectoryW(logDir.c_str(), NULL);
        return logDir;
    }
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring logDir = std::wstring(tempDir) + L"MMDirectEncoder\\logs";
    CreateDirectoryW(logDir.c_str(), NULL);
    return logDir;
}

std::wstring EncoderConfig::ResolveExecutable(const std::wstring& name, const std::wstring& customPath) {
    if (!customPath.empty() && PathFileExistsDirect(customPath)) {
        return customPath;
    }

    std::wstring dllDir = GetModuleDir(g_hInst);
    if (!dllDir.empty()) {
        std::wstring binCand = dllDir + L"\\bin\\" + name + L".exe";
        if (PathFileExistsDirect(binCand)) return binCand;
        std::wstring dirCand = dllDir + L"\\" + name + L".exe";
        if (PathFileExistsDirect(dirCand)) return dirCand;
    }

    wchar_t searchBuf[MAX_PATH] = {};
    std::wstring exeName = name + L".exe";
    DWORD found = SearchPathW(NULL, exeName.c_str(), NULL, MAX_PATH, searchBuf, NULL);
    if (found > 0 && found < MAX_PATH) {
        return std::wstring(searchBuf, found);
    }

    return name;
}

void EncoderConfig::ApplyPreset(PresetType type) {
    preset = type;
    switch (type) {
    case PresetType::HighQualityH264:
        format = L"h264";
        backend = L"auto";
        crf = 18;
        container = L"mp4";
        alpha_enabled = false;
        bit_depth = 8;
        chroma = L"yuv420p";
        colorspace = L"bt709";
        color_range = L"tv";
        gop_auto = true;
        b_frames = 3;
        break;
    case PresetType::HighQualityHEVC:
        format = L"hevc";
        backend = L"auto";
        crf = 20;
        container = L"mp4";
        alpha_enabled = false;
        bit_depth = 8;
        chroma = L"yuv420p";
        colorspace = L"bt709";
        color_range = L"tv";
        gop_auto = true;
        b_frames = 3;
        break;
    case PresetType::HighQualityAV1:
        format = L"av1";
        backend = L"auto";
        crf = 24;
        container = L"mp4";
        alpha_enabled = false;
        bit_depth = 8;
        chroma = L"yuv420p";
        colorspace = L"bt709";
        color_range = L"tv";
        gop_auto = true;
        b_frames = 3;
        break;
    case PresetType::YouTube:
        format = L"h264";
        backend = L"auto";
        crf = 18;
        container = L"mp4";
        alpha_enabled = false;
        bit_depth = 8;
        chroma = L"yuv420p";
        colorspace = L"bt709";
        color_range = L"tv";
        gop_auto = false;
        gop_size = 60;
        b_frames = 2;
        break;
    case PresetType::Editing:
        format = L"prores";
        backend = L"cpu";
        container = L"mov";
        alpha_enabled = false;
        bit_depth = 10;
        chroma = L"yuv422p10le";
        colorspace = L"bt709";
        color_range = L"tv";
        gop_auto = false;
        gop_size = 1;
        b_frames = 0;
        break;
    case PresetType::Transparent:
        alpha_enabled = true;
        alpha_format = L"prores";
        format = L"prores";
        backend = L"cpu";
        container = L"mov";
        bit_depth = 10;
        chroma = L"yuva444p10le";
        colorspace = L"bt709";
        color_range = L"tv";
        break;
    case PresetType::Lossless:
        format = L"h264";
        backend = L"auto";
        crf = 0;
        container = L"mp4";
        alpha_enabled = false;
        bit_depth = 8;
        chroma = L"yuv420p";
        colorspace = L"bt709";
        color_range = L"tv";
        break;
    case PresetType::PNGSequence:
        format = L"png";
        backend = L"cpu";
        crf = 0;
        container = L"png";
        alpha_enabled = false;
        bit_depth = 8;
        chroma = L"rgb24";
        colorspace = L"bt709";
        color_range = L"tv";
        break;
    case PresetType::Custom:
        break;
    }
}

void EncoderConfig::ValidateAndCorrect() {
    if (alpha_enabled) {
        if (alpha_format == L"prores") {
            container = L"mov";
            format = L"prores";
            backend = L"cpu";
            chroma = L"yuva444p10le";
            bit_depth = 10;
        } else if (alpha_format == L"vp9") {
            container = L"webm";
            format = L"vp9";
            backend = L"cpu";
            chroma = L"yuva420p";
            bit_depth = 8;
        } else if (alpha_format == L"png" || format == L"png") {
            container = L"png";
            format = L"png";
            backend = L"cpu";
            chroma = L"rgba";
            bit_depth = 8;
        } else {
            container = L"mkv";
            format = L"ffv1";
            backend = L"cpu";
            chroma = L"yuva444p";
            bit_depth = 8;
        }
    } else {
        if (format == L"prores") {
            container = L"mov";
        } else if (format == L"vp9") {
            container = L"webm";
        } else if (format == L"ffv1") {
            container = L"mkv";
        } else if (format == L"png") {
            container = L"png";
            backend = L"cpu";
            chroma = L"rgb24";
            bit_depth = 8;
        } else if (format == L"jpg" || format == L"jpeg") {
            container = L"jpg";
            backend = L"cpu";
            chroma = L"yuvj420p";
            bit_depth = 8;
        } else if (container.empty() || container == L"mov" || container == L"webm" || container == L"png" || container == L"jpg") {
            container = L"mp4";
        }
    }

    crf = (std::max)(0, (std::min)(51, crf));

    if (gop_size <= 0) gop_size = 250;
    b_frames = (std::max)(0, (std::min)(16, b_frames));
    lookahead = (std::max)(0, (std::min)(60, lookahead));
}

static std::wstring ToFullPath(const std::wstring& path) {
    wchar_t full[MAX_PATH] = {};
    if (GetFullPathNameW(path.c_str(), MAX_PATH, full, NULL) > 0) {
        return std::wstring(full);
    }
    return path;
}

bool EncoderConfig::Load(const std::wstring& path) {
    std::wstring ini = ToFullPath(path.empty() ? GetDefaultIniPath() : path);
    if (!PathFileExistsDirect(ini)) {
        ApplyPreset(PresetType::HighQualityH264);
        return false;
    }

    wchar_t buf[2048] = {};

    preset = static_cast<PresetType>(GetPrivateProfileIntW(L"general", L"preset", 0, ini.c_str()));
    ui_language = GetPrivateProfileIntW(L"general", L"language", 0, ini.c_str());

    GetPrivateProfileStringW(L"video", L"format", L"h264", buf, 2048, ini.c_str());
    format = LowerString(TrimString(buf));

    GetPrivateProfileStringW(L"video", L"backend", L"auto", buf, 2048, ini.c_str());
    backend = LowerString(TrimString(buf));

    crf = GetPrivateProfileIntW(L"video", L"crf", 18, ini.c_str());

    audio_enabled = GetPrivateProfileIntW(L"video", L"audio_enabled", 1, ini.c_str()) != 0;
    alpha_enabled = GetPrivateProfileIntW(L"video", L"alpha_enabled", 0, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"video", L"alpha_format", L"prores", buf, 2048, ini.c_str());
    alpha_format = LowerString(TrimString(buf));

    bit_depth = GetPrivateProfileIntW(L"advanced", L"bit_depth", 8, ini.c_str());
    GetPrivateProfileStringW(L"advanced", L"chroma", L"yuv420p", buf, 2048, ini.c_str());
    chroma = LowerString(TrimString(buf));

    GetPrivateProfileStringW(L"advanced", L"colorspace", L"bt709", buf, 2048, ini.c_str());
    colorspace = LowerString(TrimString(buf));

    GetPrivateProfileStringW(L"advanced", L"color_range", L"tv", buf, 2048, ini.c_str());
    color_range = LowerString(TrimString(buf));

    gop_auto = GetPrivateProfileIntW(L"advanced", L"gop_auto", 1, ini.c_str()) != 0;
    gop_size = GetPrivateProfileIntW(L"advanced", L"gop_size", 250, ini.c_str());
    b_frames = GetPrivateProfileIntW(L"advanced", L"b_frames", 3, ini.c_str());
    lookahead = GetPrivateProfileIntW(L"advanced", L"lookahead", 0, ini.c_str());

    GetPrivateProfileStringW(L"advanced", L"extra_args", L"", buf, 2048, ini.c_str());
    extra_args = TrimString(buf);

    GetPrivateProfileStringW(L"output", L"container", L"mp4", buf, 2048, ini.c_str());
    container = LowerString(TrimString(buf));

    delete_avi = GetPrivateProfileIntW(L"output", L"delete_avi", 1, ini.c_str()) != 0;
    merge_audio = GetPrivateProfileIntW(L"output", L"merge_audio", 1, ini.c_str()) != 0;

    GetPrivateProfileStringW(L"paths", L"ffmpeg", L"", buf, 2048, ini.c_str());
    ffmpeg_path = TrimString(buf);

    ValidateAndCorrect();
    return true;
}

bool EncoderConfig::Save(const std::wstring& path) const {
    std::wstring ini = ToFullPath(path.empty() ? GetDefaultIniPath() : path);

    auto WriteInt = [&](const wchar_t* sec, const wchar_t* key, int val) {
        WritePrivateProfileStringW(sec, key, std::to_wstring(val).c_str(), ini.c_str());
    };
    auto WriteStr = [&](const wchar_t* sec, const wchar_t* key, const std::wstring& val) {
        WritePrivateProfileStringW(sec, key, val.c_str(), ini.c_str());
    };

    WriteInt(L"general", L"preset", static_cast<int>(preset));
    WriteInt(L"general", L"language", ui_language);

    WriteStr(L"video", L"format", format);
    WriteStr(L"video", L"backend", backend);
    WriteInt(L"video", L"crf", crf);

    WriteInt(L"video", L"audio_enabled", audio_enabled ? 1 : 0);
    WriteInt(L"video", L"alpha_enabled", alpha_enabled ? 1 : 0);
    WriteStr(L"video", L"alpha_format", alpha_format);

    WriteInt(L"advanced", L"bit_depth", bit_depth);
    WriteStr(L"advanced", L"chroma", chroma);
    WriteStr(L"advanced", L"colorspace", colorspace);
    WriteStr(L"advanced", L"color_range", color_range);

    WriteInt(L"advanced", L"gop_auto", gop_auto ? 1 : 0);
    WriteInt(L"advanced", L"gop_size", gop_size);
    WriteInt(L"advanced", L"b_frames", b_frames);
    WriteInt(L"advanced", L"lookahead", lookahead);
    WriteStr(L"advanced", L"extra_args", extra_args);

    WriteStr(L"output", L"container", container);
    WriteInt(L"output", L"delete_avi", delete_avi ? 1 : 0);
    WriteInt(L"output", L"merge_audio", merge_audio ? 1 : 0);

    WriteStr(L"paths", L"ffmpeg", ffmpeg_path);

    return true;
}
