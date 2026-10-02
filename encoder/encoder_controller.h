#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "../config/encoder_config.h"

struct EncoderCapabilities {
    bool has_nvidia = false;
    bool has_intel = false;
    bool has_amd = false;
    bool nvenc_h264 = false;
    bool nvenc_hevc = false;
    bool nvenc_av1 = false;
    bool qsv_h264 = false;
    bool qsv_hevc = false;
    bool qsv_av1 = false;
    bool amf_h264 = false;
    bool amf_hevc = false;
    bool amf_av1 = false;
};

struct ExecutionPlan {
    std::wstring chosen_codec;
    std::wstring chosen_backend;
    bool fallback_occurred = false;
    std::wstring main_command;
    std::wstring primary_output_path;
    std::wstring elementary_codec;
    std::wstring elementary_muxer;
    DWORD elementary_fourcc = 0;
    bool is_image_sequence = false;
    std::wstring audio_output_path;
};

class EncoderController {
public:
    static EncoderCapabilities ProbeCapabilities(const std::wstring& ffmpegPath);
    static EncoderCapabilities GetCachedCapabilities(const std::wstring& ffmpegPath = L"", bool forceRefresh = false);

    static bool ExecuteCommand(const std::wstring& command, DWORD timeoutMs, std::string* outStderr = nullptr);

    static bool ProbeCodec(const std::wstring& ffmpegPath,
                           const std::wstring& codec,
                           const std::wstring& preset = L"",
                           const std::wstring& pixfmt = L"",
                           std::string* outError = nullptr);

    static ExecutionPlan PrepareExecutionPlan(const EncoderConfig& config,
                                             int width,
                                             int height,
                                             double fps,
                                             const std::wstring& inputPixFmt,
                                             bool bottomUp,
                                             const std::wstring& aviPath);

    static bool MergeAudio(const std::wstring& ffmpegPath,
                           const std::wstring& targetVideoPath,
                           const std::wstring& sourceAviPath,
                           bool isWebM,
                           std::string* outLog = nullptr);

    static bool ExtractAudio(const std::wstring& ffmpegPath,
                             const std::wstring& sourceAviPath,
                             const std::wstring& targetWavPath,
                             std::string* outLog = nullptr);

    static bool RunEncoderTest(const EncoderConfig& config,
                              std::wstring& outMessage);

    static void WriteExportLog(const std::wstring& logDir,
                              const ExecutionPlan& plan,
                              int width,
                              int height,
                              double fps,
                              long long frameCount,
                              DWORD exitCode,
                              double elapsedSeconds,
                              const std::string& processStderr);
};
