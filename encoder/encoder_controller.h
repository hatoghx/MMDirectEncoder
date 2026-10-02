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

struct MmdOutputInfo {
    bool valid = false;
    int start_frame = 0;
    int end_frame = 0;
    double fps = 0.0;
    bool wave_enabled = true;
    std::wstring wav_path;
};

struct ExecutionPlan {
    std::wstring chosen_codec;
    std::wstring chosen_backend;
    bool fallback_occurred = false;
    std::wstring main_command;
    std::wstring primary_output_path;
    std::wstring output_ext;
    std::wstring elementary_muxer = L"h264";
    DWORD elementary_fourcc = 0;
    bool is_image_sequence = false;
    std::wstring sequence_base;
    int sequence_start = 0;
    int sequence_digits = 0;
    bool sequence_renumber = false;
    bool native_exr = false;
    std::wstring audio_codec;
    std::wstring input_rate;
};

class EncoderController {
public:
    static const int kTempSequenceDigits = 9;

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
                                             long long frameDuration,
                                             const std::wstring& inputPixFmt,
                                             bool bottomUp,
                                             const std::wstring& aviPath,
                                             const MmdOutputInfo& mmd = MmdOutputInfo());

    static std::wstring BuildVideoArgs(const EncoderConfig& config,
                                       const std::wstring& codec,
                                       bool bottomUp);

    static bool HasAudioStream(const std::wstring& ffmpegPath, const std::wstring& mediaPath);

    static bool MergeAudio(const std::wstring& ffmpegPath,
                           const std::wstring& targetVideoPath,
                           const std::wstring& audioSourcePath,
                           double audioOffsetSeconds,
                           double durationSeconds,
                           const std::wstring& audioCodec,
                           std::string* outLog = nullptr);

    static std::wstring SequenceFramePath(const ExecutionPlan& plan, long long number);

    static bool RenumberSequence(ExecutionPlan& plan, long long frameCount);

    static int DigitCount(long long value);

    static bool RunEncoderTest(const EncoderConfig& config,
                              std::wstring& outMessage);

    static std::wstring Utf8ToWide(const std::string& s);

    static void WriteExportLog(const std::wstring& logDir,
                              const ExecutionPlan& plan,
                              const MmdOutputInfo& mmd,
                              int width,
                              int height,
                              long long frameCount,
                              DWORD exitCode,
                              double elapsedSeconds,
                              const std::wstring& result,
                              const std::string& processStderr);
};
