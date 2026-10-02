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
        std::wstring baseDir = std::wstring(localAppData) + L"\\MMDirectEncoder";
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
        std::wstring logDir = std::wstring(localAppData) + L"\\MMDirectEncoder\\logs";
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
        format = L"prores422hq";
        alpha_enabled = false;
        colorspace = L"bt709";
        color_range = L"tv";
        break;
    case PresetType::Transparent:
        alpha_enabled = true;
        alpha_format = L"prores4444";
        colorspace = L"bt709";
        color_range = L"tv";
        break;
    case PresetType::PNGSequence:
        format = L"png";
        alpha_enabled = false;
        break;
    case PresetType::LegacyLossless:
    case PresetType::LegacyUtVideo:
    case PresetType::Custom:
        break;
    }
    ValidateAndCorrect();
}

namespace {
    std::wstring BaseChroma(std::wstring chroma) {
        if (chroma.size() > 4 && chroma.compare(chroma.size() - 4, 4, L"10le") == 0) {
            chroma.erase(chroma.size() - 4);
        }
        if (chroma != L"yuv420p" && chroma != L"yuv422p" && chroma != L"yuv444p") {
            chroma = L"yuv420p";
        }
        return chroma;
    }

    bool IsVideoFormat(const std::wstring& f) {
        return f == L"h264" || f == L"hevc" || f == L"av1" || f == L"prores422" || f == L"prores422hq" || f == L"vp9" || f == L"av1webm";
    }

    bool IsAlphaFormat(const std::wstring& f) {
        return f == L"prores4444" || f == L"prores4444xq" || f == L"vp9" || f == L"png" || f == L"exr";
    }
}

std::wstring EncoderConfig::EffectiveFormat() const {
    return alpha_enabled ? alpha_format : format;
}

int EncoderConfig::MaxQuality(const std::wstring& fmt) {
    return (fmt == L"vp9" || fmt == L"av1" || fmt == L"av1webm") ? 63 : 51;
}

bool EncoderConfig::IsImageSequence() const {
    std::wstring f = EffectiveFormat();
    return f == L"jpg" || f == L"png" || f == L"exr";
}

bool EncoderConfig::IsProRes() const {
    return EffectiveFormat().compare(0, 6, L"prores") == 0;
}

bool EncoderConfig::UsesYuv() const {
    std::wstring f = EffectiveFormat();
    return f == L"h264" || f == L"hevc" || f == L"av1" || f == L"av1webm" || f == L"vp9" || IsProRes();
}

bool EncoderConfig::UsesQuality() const {
    std::wstring f = EffectiveFormat();
    return f == L"h264" || f == L"hevc" || f == L"av1" || f == L"av1webm" || f == L"vp9";
}

bool EncoderConfig::UsesHardwareBackend() const {
    return !alpha_enabled && (format == L"h264" || format == L"hevc" || format == L"av1" || format == L"av1webm");
}

void EncoderConfig::ValidateAndCorrect() {
    if (format == L"prores") format = L"prores422hq";
    if (format == L"jpeg") format = L"jpg";
    if (alpha_format == L"prores") alpha_format = L"prores4444";
    if (!IsVideoFormat(format) && format != L"jpg" && format != L"png" && format != L"exr") {
        format = L"h264";
    }
    if (!IsAlphaFormat(alpha_format)) {
        alpha_format = L"prores4444";
    }

    std::wstring f = EffectiveFormat();
    if (f == L"h264" || f == L"hevc" || f == L"av1" || f == L"av1webm") {
        container = (f == L"av1webm") ? L"webm" : L"mp4";
        chroma = BaseChroma(chroma);
        if (f == L"av1" || f == L"av1webm") chroma = L"yuv420p";
    } else if (IsProRes()) {
        container = L"mov";
        backend = L"cpu";
    } else if (f == L"vp9") {
        container = L"webm";
        backend = L"cpu";
        if (!alpha_enabled) chroma = BaseChroma(chroma);
    } else {
        container = f;
        backend = L"cpu";
    }

    if (backend != L"auto" && backend != L"nvidia" && backend != L"intel" && backend != L"amd" && backend != L"cpu") {
        backend = L"auto";
    }
    if (colorspace != L"bt709" && colorspace != L"bt601") {
        colorspace = L"bt709";
    }
    if (color_range != L"tv" && color_range != L"pc") {
        color_range = L"tv";
    }
    if (bit_depth != 8 && bit_depth != 10) {
        bit_depth = 8;
    }

    crf = (std::max)(0, (std::min)(MaxQuality(f), crf));

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

    int presetValue = GetPrivateProfileIntW(L"general", L"preset", 0, ini.c_str());
    preset = (presetValue >= 0 && presetValue <= static_cast<int>(PresetType::LegacyUtVideo)) ? static_cast<PresetType>(presetValue) : PresetType::Custom;
    if (preset == PresetType::LegacyLossless || preset == PresetType::LegacyUtVideo) preset = PresetType::Custom;
    ui_language = GetPrivateProfileIntW(L"general", L"language", 0, ini.c_str());
    if (ui_language < 0 || ui_language > 2) ui_language = 0;

    GetPrivateProfileStringW(L"video", L"format", L"h264", buf, 2048, ini.c_str());
    format = LowerString(TrimString(buf));

    GetPrivateProfileStringW(L"video", L"backend", L"auto", buf, 2048, ini.c_str());
    backend = LowerString(TrimString(buf));

    crf = GetPrivateProfileIntW(L"video", L"crf", 18, ini.c_str());

    audio_enabled = GetPrivateProfileIntW(L"video", L"audio_enabled", 1, ini.c_str()) != 0;
    alpha_enabled = GetPrivateProfileIntW(L"video", L"alpha_enabled", 0, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"video", L"alpha_format", L"prores4444", buf, 2048, ini.c_str());
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

    GetPrivateProfileStringW(L"output", L"container", L"mp4", buf, 2048, ini.c_str());
    container = LowerString(TrimString(buf));

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

    WriteStr(L"output", L"container", container);
    WriteInt(L"output", L"merge_audio", merge_audio ? 1 : 0);

    WriteStr(L"paths", L"ffmpeg", ffmpeg_path);

    return true;
}
