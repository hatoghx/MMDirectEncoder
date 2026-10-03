#pragma once

#include <windows.h>
#include <string>

enum class PresetType {
    HighQualityH264 = 0,
    HighQualityHEVC,
    HighQualityAV1,
    YouTube,
    Editing,
    Transparent,
    LegacyLossless,
    PNGSequence,
    Custom,
    LegacyUtVideo
};

struct EncoderConfig {
    PresetType preset = PresetType::HighQualityH264;
    std::wstring format = L"h264";
    std::wstring backend = L"auto";
    int crf = 18;
    bool audio_enabled = true;
    bool alpha_enabled = false;
    std::wstring alpha_format = L"prores4444";

    int bit_depth = 8;
    std::wstring chroma = L"yuv420p";
    std::wstring colorspace = L"bt709";
    std::wstring color_range = L"tv";

    bool gop_auto = true;
    int gop_size = 250;
    int b_frames = 3;
    int lookahead = 0;

    std::wstring container = L"mp4";
    bool merge_audio = true;

    std::wstring ffmpeg_path;
    int ui_language = 0;

    static std::wstring GetDefaultIniPath();
    static std::wstring GetLogDirectoryPath();
    static std::wstring ResolveExecutable(const std::wstring& name, const std::wstring& customPath);
    static int MaxQuality(const std::wstring& format);
    static std::wstring NormalizeChroma(std::wstring value);
    int ResolvedLanguage() const;
    void ApplyPreset(PresetType type);
    void ValidateAndCorrect();
    std::wstring EffectiveFormat() const;
    bool IsImageSequence() const;
    bool IsProRes() const;
    bool UsesYuv() const;
    bool UsesQuality() const;
    bool UsesHardwareBackend() const;
    bool Load(const std::wstring& path = L"");
    bool Save(const std::wstring& path = L"") const;
};
