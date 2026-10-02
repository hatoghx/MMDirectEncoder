#include "encoder_controller.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <dxgi.h>

namespace {
    void AppendQuoted(std::wstring& s, const std::wstring& arg) {
        if (!s.empty()) {
            s += L' ';
        }
        if (arg.empty() || arg.find(L' ') != std::wstring::npos) {
            s += L'"';
            s += arg;
            s += L'"';
        } else {
            s += arg;
        }
    }

    std::wstring EscapeTeePath(std::wstring path) {
        std::replace(path.begin(), path.end(), L'\\', L'/');
        std::wstring out;
        for (wchar_t ch : path) {
            if (ch == L'[' || ch == L']' || ch == L'|' || ch == L':' || ch == L'\'') {
                out += L'\\';
            }
            out += ch;
        }
        return out;
    }

    DWORD GetFourcc(char a, char b, char c, char d) {
        return (static_cast<DWORD>(static_cast<BYTE>(a))) |
               (static_cast<DWORD>(static_cast<BYTE>(b)) << 8) |
               (static_cast<DWORD>(static_cast<BYTE>(c)) << 16) |
               (static_cast<DWORD>(static_cast<BYTE>(d)) << 24);
    }

    bool PathFileExistsDirect(const std::wstring& path) {
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    struct DxgiGpuVendors {
        bool has_nvidia = false;
        bool has_intel = false;
        bool has_amd = false;
    };

    DxgiGpuVendors DetectDxgiVendors() {
        DxgiGpuVendors v;
        IDXGIFactory* pFactory = nullptr;
        if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&pFactory))) || !pFactory) {
            v.has_nvidia = true;
            v.has_intel = true;
            v.has_amd = true;
            return v;
        }
        IDXGIAdapter* pAdapter = nullptr;
        UINT i = 0;
        while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
            if (pAdapter) {
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                    if (desc.VendorId == 0x10DE) v.has_nvidia = true;
                    else if (desc.VendorId == 0x8086) v.has_intel = true;
                    else if (desc.VendorId == 0x1002) v.has_amd = true;
                }
                pAdapter->Release();
            }
            i++;
        }
        pFactory->Release();

        if (!v.has_nvidia && !v.has_intel && !v.has_amd) {
            v.has_nvidia = true;
            v.has_intel = true;
            v.has_amd = true;
        }
        return v;
    }
}

bool EncoderController::ExecuteCommand(const std::wstring& command, DWORD timeoutMs, std::string* outStderr) {
    if (command.empty()) return false;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hStdErrRd = NULL, hStdErrWr = NULL;
    if (outStderr) {
        if (!CreatePipe(&hStdErrRd, &hStdErrWr, &sa, 0)) {
            return false;
        }
        SetHandleInformation(hStdErrRd, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (outStderr) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdError = hStdErrWr;
    }

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmd = command;
    BOOL ok = CreateProcessW(NULL, cmd.data(), NULL, NULL, outStderr ? TRUE : FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    if (hStdErrWr) {
        CloseHandle(hStdErrWr);
    }

    if (!ok) {
        if (hStdErrRd) CloseHandle(hStdErrRd);
        return false;
    }

    CloseHandle(pi.hThread);

    if (outStderr && hStdErrRd) {
        char buffer[4096];
        DWORD readBytes = 0;
        while (ReadFile(hStdErrRd, buffer, sizeof(buffer), &readBytes, NULL) && readBytes > 0) {
            outStderr->append(buffer, readBytes);
        }
        CloseHandle(hStdErrRd);
    }

    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        return false;
    }

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return code == 0;
}

bool EncoderController::ProbeCodec(const std::wstring& ffmpegPath,
                                   const std::wstring& codec,
                                   const std::wstring& preset,
                                   const std::wstring& pixfmt,
                                   std::string* outError) {
    if (codec == L"png") {
        std::wstring cmd;
        AppendQuoted(cmd, ffmpegPath);
        cmd += L" -hide_banner -loglevel error -nostdin -f lavfi -i color=c=black:s=64x64:d=0.1 -frames:v 1 -c:v png -f null -";
        return ExecuteCommand(cmd, 15000, outError);
    }

    std::wstring cmd;
    AppendQuoted(cmd, ffmpegPath);
    cmd += L" -hide_banner -loglevel error -nostdin";
    if (codec.find(L"qsv") != std::wstring::npos) {
        cmd += L" -init_hw_device qsv=hw -filter_hw_device hw";
    }
    cmd += L" -f lavfi -i color=c=black:s=256x256:d=0.1 -frames:v 1 -c:v ";
    cmd += codec;
    if (!preset.empty()) {
        cmd += (codec.find(L"amf") != std::wstring::npos) ? L" -quality " : L" -preset ";
        cmd += preset;
    }
    if (!pixfmt.empty()) {
        cmd += L" -pix_fmt ";
        cmd += pixfmt;
    }
    cmd += L" -f null -";

    return ExecuteCommand(cmd, 15000, outError);
}

EncoderCapabilities EncoderController::ProbeCapabilities(const std::wstring& ffmpegPath) {
    EncoderCapabilities caps;
    std::wstring resolvedFfmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", ffmpegPath);

    DxgiGpuVendors gpus = DetectDxgiVendors();
    caps.has_nvidia = gpus.has_nvidia;
    caps.has_intel = gpus.has_intel;
    caps.has_amd = gpus.has_amd;

    if (caps.has_nvidia) {
        caps.nvenc_h264 = ProbeCodec(resolvedFfmpeg, L"h264_nvenc", L"p4", L"yuv420p");
        caps.nvenc_hevc = ProbeCodec(resolvedFfmpeg, L"hevc_nvenc", L"p4", L"yuv420p");
        caps.nvenc_av1  = ProbeCodec(resolvedFfmpeg, L"av1_nvenc", L"p4", L"yuv420p");
    }

    if (caps.has_intel) {
        caps.qsv_h264 = ProbeCodec(resolvedFfmpeg, L"h264_qsv", L"medium", L"nv12");
        caps.qsv_hevc = ProbeCodec(resolvedFfmpeg, L"hevc_qsv", L"medium", L"nv12");
        caps.qsv_av1  = ProbeCodec(resolvedFfmpeg, L"av1_qsv", L"medium", L"nv12");
    }

    if (caps.has_amd) {
        caps.amf_h264 = ProbeCodec(resolvedFfmpeg, L"h264_amf", L"balanced", L"yuv420p");
        caps.amf_hevc = ProbeCodec(resolvedFfmpeg, L"hevc_amf", L"balanced", L"yuv420p");
        caps.amf_av1  = ProbeCodec(resolvedFfmpeg, L"av1_amf", L"balanced", L"yuv420p");
    }

    return caps;
}

EncoderCapabilities EncoderController::GetCachedCapabilities(const std::wstring& ffmpegPath, bool forceRefresh) {
    std::wstring ini = EncoderConfig::GetDefaultIniPath();
    if (!forceRefresh) {
        int cached = GetPrivateProfileIntW(L"hardware", L"cached", 0, ini.c_str());
        if (cached == 1) {
            EncoderCapabilities caps;
            caps.has_nvidia = GetPrivateProfileIntW(L"hardware", L"has_nvidia", 0, ini.c_str()) != 0;
            caps.has_intel = GetPrivateProfileIntW(L"hardware", L"has_intel", 0, ini.c_str()) != 0;
            caps.has_amd = GetPrivateProfileIntW(L"hardware", L"has_amd", 0, ini.c_str()) != 0;
            caps.nvenc_h264 = GetPrivateProfileIntW(L"hardware", L"nvenc_h264", 0, ini.c_str()) != 0;
            caps.nvenc_hevc = GetPrivateProfileIntW(L"hardware", L"nvenc_hevc", 0, ini.c_str()) != 0;
            caps.nvenc_av1  = GetPrivateProfileIntW(L"hardware", L"nvenc_av1", 0, ini.c_str()) != 0;
            caps.qsv_h264   = GetPrivateProfileIntW(L"hardware", L"qsv_h264", 0, ini.c_str()) != 0;
            caps.qsv_hevc   = GetPrivateProfileIntW(L"hardware", L"qsv_hevc", 0, ini.c_str()) != 0;
            caps.qsv_av1    = GetPrivateProfileIntW(L"hardware", L"qsv_av1", 0, ini.c_str()) != 0;
            caps.amf_h264   = GetPrivateProfileIntW(L"hardware", L"amf_h264", 0, ini.c_str()) != 0;
            caps.amf_hevc   = GetPrivateProfileIntW(L"hardware", L"amf_hevc", 0, ini.c_str()) != 0;
            caps.amf_av1    = GetPrivateProfileIntW(L"hardware", L"amf_av1", 0, ini.c_str()) != 0;
            return caps;
        }
    }

    EncoderCapabilities caps = ProbeCapabilities(ffmpegPath);
    WritePrivateProfileStringW(L"hardware", L"cached", L"1", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"has_nvidia", caps.has_nvidia ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"has_intel", caps.has_intel ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"has_amd", caps.has_amd ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"nvenc_h264", caps.nvenc_h264 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"nvenc_hevc", caps.nvenc_hevc ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"nvenc_av1", caps.nvenc_av1 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"qsv_h264", caps.qsv_h264 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"qsv_hevc", caps.qsv_hevc ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"qsv_av1", caps.qsv_av1 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"amf_h264", caps.amf_h264 ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"amf_hevc", caps.amf_hevc ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hardware", L"amf_av1", caps.amf_av1 ? L"1" : L"0", ini.c_str());
    return caps;
}

ExecutionPlan EncoderController::PrepareExecutionPlan(const EncoderConfig& config,
                                                     int width,
                                                     int height,
                                                     double fps,
                                                     const std::wstring& inputPixFmt,
                                                     bool bottomUp,
                                                     const std::wstring& aviPath) {
    ExecutionPlan plan;
    std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", config.ffmpeg_path);
    EncoderCapabilities caps = GetCachedCapabilities(ffmpeg);

    std::wstring targetCodec;
    std::wstring targetBackend = config.backend;

    if (config.format == L"png" || (config.alpha_enabled && config.alpha_format == L"png")) {
        targetCodec = L"png";
        targetBackend = L"cpu";
    } else if (config.format == L"jpg" || config.format == L"jpeg") {
        targetCodec = L"mjpeg";
        targetBackend = L"cpu";
    } else if (config.alpha_enabled) {
        if (config.alpha_format == L"prores") {
            targetCodec = L"prores_ks";
            targetBackend = L"cpu";
        } else if (config.alpha_format == L"vp9") {
            targetCodec = L"libvpx-vp9";
            targetBackend = L"cpu";
        } else {
            targetCodec = L"ffv1";
            targetBackend = L"cpu";
        }
    } else if (config.format == L"hevc") {
        if (targetBackend == L"auto") {
            if (caps.nvenc_hevc) { targetCodec = L"hevc_nvenc"; targetBackend = L"nvidia"; }
            else if (caps.qsv_hevc) { targetCodec = L"hevc_qsv"; targetBackend = L"intel"; }
            else if (caps.amf_hevc) { targetCodec = L"hevc_amf"; targetBackend = L"amd"; }
            else { targetCodec = L"libx265"; targetBackend = L"cpu"; }
        } else if (targetBackend == L"nvidia") {
            targetCodec = caps.nvenc_hevc ? L"hevc_nvenc" : L"libx265";
            if (!caps.nvenc_hevc) plan.fallback_occurred = true;
        } else if (targetBackend == L"intel") {
            targetCodec = caps.qsv_hevc ? L"hevc_qsv" : L"libx265";
            if (!caps.qsv_hevc) plan.fallback_occurred = true;
        } else if (targetBackend == L"amd") {
            targetCodec = caps.amf_hevc ? L"hevc_amf" : L"libx265";
            if (!caps.amf_hevc) plan.fallback_occurred = true;
        } else {
            targetCodec = L"libx265";
        }
    } else if (config.format == L"av1") {
        if (targetBackend == L"auto") {
            if (caps.nvenc_av1) { targetCodec = L"av1_nvenc"; targetBackend = L"nvidia"; }
            else if (caps.qsv_av1) { targetCodec = L"av1_qsv"; targetBackend = L"intel"; }
            else if (caps.amf_av1) { targetCodec = L"av1_amf"; targetBackend = L"amd"; }
            else { targetCodec = L"libsvtav1"; targetBackend = L"cpu"; }
        } else if (targetBackend == L"nvidia") {
            targetCodec = caps.nvenc_av1 ? L"av1_nvenc" : L"libsvtav1";
            if (!caps.nvenc_av1) plan.fallback_occurred = true;
        } else if (targetBackend == L"intel") {
            targetCodec = caps.qsv_av1 ? L"av1_qsv" : L"libsvtav1";
            if (!caps.qsv_av1) plan.fallback_occurred = true;
        } else if (targetBackend == L"amd") {
            targetCodec = caps.amf_av1 ? L"av1_amf" : L"libsvtav1";
            if (!caps.amf_av1) plan.fallback_occurred = true;
        } else {
            targetCodec = L"libsvtav1";
        }
    } else if (config.format == L"prores") {
        targetCodec = L"prores_ks";
        targetBackend = L"cpu";
    } else if (config.format == L"vp9") {
        targetCodec = L"libvpx-vp9";
        targetBackend = L"cpu";
    } else {
        if (targetBackend == L"auto") {
            if (caps.nvenc_h264) { targetCodec = L"h264_nvenc"; targetBackend = L"nvidia"; }
            else if (caps.qsv_h264) { targetCodec = L"h264_qsv"; targetBackend = L"intel"; }
            else if (caps.amf_h264) { targetCodec = L"h264_amf"; targetBackend = L"amd"; }
            else { targetCodec = L"libx264"; targetBackend = L"cpu"; }
        } else if (targetBackend == L"nvidia") {
            targetCodec = caps.nvenc_h264 ? L"h264_nvenc" : L"libx264";
            if (!caps.nvenc_h264) plan.fallback_occurred = true;
        } else if (targetBackend == L"intel") {
            targetCodec = caps.qsv_h264 ? L"h264_qsv" : L"libx264";
            if (!caps.qsv_h264) plan.fallback_occurred = true;
        } else if (targetBackend == L"amd") {
            targetCodec = caps.amf_h264 ? L"h264_amf" : L"libx264";
            if (!caps.amf_h264) plan.fallback_occurred = true;
        } else {
            targetCodec = L"libx264";
        }
    }

    plan.chosen_codec = targetCodec;
    plan.chosen_backend = targetBackend;

    plan.is_image_sequence = (config.format == L"png" || config.format == L"jpg" || config.format == L"jpeg" ||
                              config.container == L"png" || config.container == L"jpg" || config.container == L"jpeg" ||
                              (config.alpha_enabled && config.alpha_format == L"png"));
    std::wstring outExt = config.container;
    if (config.alpha_enabled) {
        if (config.alpha_format == L"prores") outExt = L"mov";
        else if (config.alpha_format == L"vp9") outExt = L"webm";
        else if (config.alpha_format == L"png" || config.format == L"png") outExt = L"png";
        else outExt = L"mkv";
    } else {
        if (config.format == L"jpg" || config.format == L"jpeg" || config.container == L"jpg" || config.container == L"jpeg") {
            outExt = L"jpg";
        }
    }

    std::wstring mainOutPath;
    if (!aviPath.empty()) {
        size_t dot = aviPath.find_last_of(L'.');
        size_t slash = aviPath.find_last_of(L"\\/");
        size_t cut = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) ? dot : aviPath.size();
        std::wstring base = aviPath.substr(0, cut);
        if (plan.is_image_sequence) {
            mainOutPath = base + L"_%05d." + outExt;
            plan.primary_output_path = base + L"_00000." + outExt;
            plan.audio_output_path = base + L".wav";
        } else {
            mainOutPath = base + L"." + outExt;
            plan.primary_output_path = mainOutPath;
        }
    }

    if (config.format == L"hevc" && (targetCodec.find(L"hevc") != std::wstring::npos || targetCodec.find(L"265") != std::wstring::npos)) {
        plan.elementary_codec = L"hevc";
        plan.elementary_muxer = L"hevc";
        plan.elementary_fourcc = GetFourcc('H', 'E', 'V', 'C');
    } else {
        plan.elementary_codec = L"h264";
        plan.elementary_muxer = L"h264";
        plan.elementary_fourcc = GetFourcc('H', '2', '6', '4');
    }

    std::wstring cmd;
    AppendQuoted(cmd, ffmpeg);
    cmd += L" -y -hide_banner -loglevel error -nostdin";

    if (targetCodec.find(L"qsv") != std::wstring::npos) {
        cmd += L" -init_hw_device qsv=hw -filter_hw_device hw";
    }

    cmd += L" -f rawvideo -pix_fmt ";
    cmd += inputPixFmt;
    cmd += L" -s " + std::to_wstring(width) + L"x" + std::to_wstring(height);
    wchar_t fpsBuf[32];
    swprintf_s(fpsBuf, L"%g", fps);
    cmd += L" -r ";
    cmd += fpsBuf;
    cmd += L" -i pipe:0";

    std::vector<std::wstring> filters;
    if (bottomUp) {
        filters.push_back(L"vflip");
    }
    std::wstring vfArg;
    if (!filters.empty()) {
        vfArg = L" -vf \"";
        for (size_t i = 0; i < filters.size(); ++i) {
            if (i > 0) vfArg += L",";
            vfArg += filters[i];
        }
        vfArg += L"\"";
    }

    auto BuildCodecParamString = [&](const std::wstring& codec) -> std::wstring {
        if (codec == L"png") {
            std::wstring px = config.alpha_enabled ? L"rgba" : L"rgb24";
            std::wstring p = L" -c:v png -pix_fmt " + px;
            if (!config.extra_args.empty()) {
                p += L" " + config.extra_args;
            }
            return p;
        }
        if (codec == L"mjpeg") {
            std::wstring p = L" -c:v mjpeg -q:v 2";
            if (!config.extra_args.empty()) {
                p += L" " + config.extra_args;
            }
            return p;
        }

        std::wstring p = L" -c:v " + codec;
        bool isGpu = (codec.find(L"nvenc") != std::wstring::npos ||
                      codec.find(L"qsv") != std::wstring::npos ||
                      codec.find(L"amf") != std::wstring::npos);

        if (config.preset == PresetType::Lossless || config.crf == 0) {
            if (codec.find(L"nvenc") != std::wstring::npos) {
                p += L" -preset lossless";
            } else if (codec.find(L"qsv") != std::wstring::npos) {
                p += L" -global_quality 1";
            } else if (codec.find(L"amf") != std::wstring::npos) {
                p += L" -qp_i 0 -qp_p 0";
            } else if (codec == L"libx264" || codec == L"libx265") {
                p += L" -crf 0";
            }
        } else {
            if (codec.find(L"nvenc") != std::wstring::npos) {
                p += L" -cq " + std::to_wstring(config.crf);
            } else if (codec.find(L"qsv") != std::wstring::npos) {
                p += L" -global_quality " + std::to_wstring(config.crf);
            } else if (codec.find(L"amf") != std::wstring::npos) {
                p += L" -qp_i " + std::to_wstring(config.crf) + L" -qp_p " + std::to_wstring(config.crf);
            } else {
                p += L" -crf " + std::to_wstring(config.crf);
            }
        }

        if (config.alpha_enabled) {
            if (config.alpha_format == L"prores") {
                p += L" -pix_fmt yuva444p10le -profile:v 4444 -vendor apl0";
            } else if (config.alpha_format == L"vp9") {
                p += L" -pix_fmt yuva420p -deadline good -cpu-used 2";
            } else {
                p += L" -pix_fmt yuva444p -level 3";
            }
        } else {
            std::wstring px = config.chroma;
            if (config.bit_depth == 10 && px.find(L"10le") == std::wstring::npos) {
                px += L"10le";
            }
            if (isGpu && (codec.find(L"qsv") != std::wstring::npos)) {
                px = (config.bit_depth == 10) ? L"p010le" : L"nv12";
            }
            p += L" -pix_fmt " + px;
        }

        if (config.colorspace == L"bt2020") {
            p += L" -colorspace bt2020nc -color_primaries bt2020 -color_trc bt2020-10";
        } else if (config.colorspace == L"bt601") {
            p += L" -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m";
        } else {
            p += L" -colorspace bt709 -color_primaries bt709 -color_trc bt709";
        }
        p += (config.color_range == L"pc") ? L" -color_range pc" : L" -color_range tv";

        if (!config.gop_auto && config.gop_size > 0) {
            p += L" -g " + std::to_wstring(config.gop_size);
        }
        if (config.b_frames >= 0 && !isGpu && (codec == L"libx264" || codec == L"libx265" || codec == L"libvpx-vp9" || codec == L"libsvtav1")) {
            p += L" -bf " + std::to_wstring(config.b_frames);
        }
        if (config.lookahead > 0 && codec.find(L"nvenc") != std::wstring::npos) {
            p += L" -rc-lookahead " + std::to_wstring(config.lookahead);
        }

        if (!config.extra_args.empty()) {
            p += L" " + config.extra_args;
        }

        return p;
    };

    std::wstring mainCodecParams = BuildCodecParamString(targetCodec);

    if (config.alpha_enabled || !mainOutPath.empty()) {
        cmd += L" -map 0:v";
        cmd += vfArg;
        if (plan.is_image_sequence) {
            cmd += L" -start_number 0";
        }
        cmd += mainCodecParams;
        AppendQuoted(cmd, mainOutPath);

        cmd += L" -map 0:v";
        cmd += vfArg;
        cmd += L" -c:v libx264 -preset ultrafast -pix_fmt yuv420p -f " + plan.elementary_muxer + L" pipe:1";
    } else {
        cmd += L" -map 0:v";
        cmd += vfArg;
        cmd += mainCodecParams;
        cmd += L" -f " + plan.elementary_muxer + L" pipe:1";
    }

    plan.main_command = cmd;
    return plan;
}

bool EncoderController::MergeAudio(const std::wstring& ffmpegPath,
                                   const std::wstring& targetVideoPath,
                                   const std::wstring& sourceAviPath,
                                   bool isWebM,
                                   std::string* outLog) {
    if (targetVideoPath.empty() || sourceAviPath.empty() || !PathFileExistsDirect(targetVideoPath) || !PathFileExistsDirect(sourceAviPath)) {
        return false;
    }

    size_t dot = targetVideoPath.find_last_of(L'.');
    std::wstring tempMerged = (dot == std::wstring::npos) ? (targetVideoPath + L".tmp_merge.mp4") : (targetVideoPath.substr(0, dot) + L".tmp_merge" + targetVideoPath.substr(dot));

    std::wstring cmd;
    AppendQuoted(cmd, ffmpegPath);
    cmd += L" -y -hide_banner -loglevel error -nostdin -i";
    AppendQuoted(cmd, targetVideoPath);
    cmd += L" -i";
    AppendQuoted(cmd, sourceAviPath);
    cmd += L" -map 0:v:0 -map 1:a:0? -c:v copy -c:a ";
    cmd += isWebM ? L"libopus" : L"aac";
    cmd += L" -b:a 192k -shortest";
    AppendQuoted(cmd, tempMerged);

    bool ok = ExecuteCommand(cmd, 120000, outLog);
    if (!ok || !PathFileExistsDirect(tempMerged)) {
        DeleteFileW(tempMerged.c_str());
        return false;
    }

    if (!MoveFileExW(tempMerged.c_str(), targetVideoPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempMerged.c_str());
        return false;
    }

    return true;
}

bool EncoderController::ExtractAudio(const std::wstring& ffmpegPath,
                                     const std::wstring& sourceAviPath,
                                     const std::wstring& targetWavPath,
                                     std::string* outLog) {
    if (sourceAviPath.empty() || targetWavPath.empty() || !PathFileExistsDirect(sourceAviPath)) {
        return false;
    }

    std::wstring cmd;
    AppendQuoted(cmd, ffmpegPath);
    cmd += L" -y -hide_banner -loglevel error -nostdin -i";
    AppendQuoted(cmd, sourceAviPath);
    cmd += L" -map 0:a:0 -c:a pcm_s16le";
    AppendQuoted(cmd, targetWavPath);

    return ExecuteCommand(cmd, 120000, outLog) && PathFileExistsDirect(targetWavPath);
}

bool EncoderController::RunEncoderTest(const EncoderConfig& config, std::wstring& outMessage) {
    int lang = config.ui_language;
    if (lang == 0) {
        LANGID sysLang = PRIMARYLANGID(GetUserDefaultUILanguage());
        if (sysLang == LANG_CHINESE) lang = 3;
        else if (sysLang == LANG_JAPANESE) lang = 1;
        else lang = 2;
    }

    std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", config.ffmpeg_path);
    if (!PathFileExistsDirect(ffmpeg) && ffmpeg != L"ffmpeg") {
        if (lang == 3) outMessage = L"未找到 ffmpeg.exe。";
        else if (lang == 2) outMessage = L"ffmpeg.exe was not found.";
        else outMessage = L"ffmpeg.exe が見つかりません。";
        return false;
    }

    GetCachedCapabilities(config.ffmpeg_path, true);
    ExecutionPlan plan = PrepareExecutionPlan(config, 256, 256, 30.0, L"bgr24", false, L"test.avi");
    std::string err;
    bool ok = ProbeCodec(ffmpeg, plan.chosen_codec, L"", config.chroma, &err);

    if (ok) {
        if (lang == 3) {
            outMessage = L"测试成功: " + plan.chosen_codec + L" 正常可用。";
            if (plan.fallback_occurred) {
                outMessage += L" (因指定的GPU不可用，已自动回退到CPU)";
            }
        } else if (lang == 2) {
            outMessage = L"Test Successful: " + plan.chosen_codec + L" is available.";
            if (plan.fallback_occurred) {
                outMessage += L" (Fell back to CPU as specified GPU is unavailable)";
            }
        } else {
            outMessage = L"テスト成功: " + plan.chosen_codec + L" は正常に利用可能です。";
            if (plan.fallback_occurred) {
                outMessage += L" (指定GPUが利用できないためCPUにフォールバックしました)";
            }
        }
        return true;
    } else {
        std::wstring wErr(err.begin(), err.end());
        if (lang == 3) {
            outMessage = L"测试失败: " + plan.chosen_codec + L"\n" + wErr;
        } else if (lang == 2) {
            outMessage = L"Test Failed: " + plan.chosen_codec + L"\n" + wErr;
        } else {
            outMessage = L"テスト失敗: " + plan.chosen_codec + L"\n" + wErr;
        }
        return false;
    }
}

void EncoderController::WriteExportLog(const std::wstring& logDir,
                                       const ExecutionPlan& plan,
                                       int width,
                                       int height,
                                       double fps,
                                       long long frameCount,
                                       DWORD exitCode,
                                       double elapsedSeconds,
                                       const std::string& processStderr) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmNow;
    localtime_s(&tmNow, &t);

    std::wostringstream nameOss;
    nameOss << std::setfill(L'0') << logDir << L"\\export_"
            << std::setw(4) << (tmNow.tm_year + 1900)
            << std::setw(2) << (tmNow.tm_mon + 1)
            << std::setw(2) << tmNow.tm_mday << L"_"
            << std::setw(2) << tmNow.tm_hour
            << std::setw(2) << tmNow.tm_min
            << std::setw(2) << tmNow.tm_sec << L".log";

    std::wstring logFile = nameOss.str();
    std::wstring latestFile = logDir + L"\\latest.log";

    auto writeContent = [&](std::wofstream& ofs) {
        ofs << L"=== MMDirect Encoder Export Diagnostic Log ===" << std::endl;
        ofs << L"Date: " << (tmNow.tm_year + 1900) << L"-" << (tmNow.tm_mon + 1) << L"-" << tmNow.tm_mday
            << L" " << tmNow.tm_hour << L":" << tmNow.tm_min << L":" << tmNow.tm_sec << std::endl;
        ofs << L"Chosen Codec: " << plan.chosen_codec << std::endl;
        ofs << L"Chosen Backend: " << plan.chosen_backend << std::endl;
        ofs << L"Fallback Occurred: " << (plan.fallback_occurred ? L"Yes" : L"No") << std::endl;
        ofs << L"Input Resolution: " << width << L"x" << height << std::endl;
        ofs << L"Input FPS: " << fps << std::endl;
        ofs << L"Processed Frames: " << frameCount << std::endl;
        ofs << L"Elapsed Time: " << elapsedSeconds << L" s" << std::endl;
        ofs << L"Exit Code: " << exitCode << std::endl;
        ofs << L"Output File: " << plan.primary_output_path << std::endl;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(plan.primary_output_path.c_str(), GetFileExInfoStandard, &fad)) {
            LARGE_INTEGER sz;
            sz.HighPart = fad.nFileSizeHigh;
            sz.LowPart = fad.nFileSizeLow;
            ofs << L"Output File Size: " << (sz.QuadPart / 1024) << L" KB" << std::endl;
        }

        ofs << L"Full Command: " << plan.main_command << std::endl;
        if (!processStderr.empty()) {
            std::wstring wErr(processStderr.begin(), processStderr.end());
            ofs << L"Stderr Output:\n" << wErr << std::endl;
        }
    };

    std::wofstream ofs1(logFile);
    if (ofs1.is_open()) {
        writeContent(ofs1);
    }
    std::wofstream ofs2(latestFile);
    if (ofs2.is_open()) {
        writeContent(ofs2);
    }
}
