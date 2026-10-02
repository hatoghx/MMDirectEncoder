#include "encoder_controller.h"
#include "exr_writer.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
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

namespace {
    std::wstring CapabilitySignature(const std::wstring& ffmpegPath) {
        std::wostringstream oss;
        oss << ffmpegPath;
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(ffmpegPath.c_str(), GetFileExInfoStandard, &fad)) {
            oss << L"|" << fad.ftLastWriteTime.dwHighDateTime << L"." << fad.ftLastWriteTime.dwLowDateTime
                << L"|" << fad.nFileSizeLow;
        }
        IDXGIFactory* pFactory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&pFactory))) && pFactory) {
            IDXGIAdapter* pAdapter = nullptr;
            for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                if (!pAdapter) continue;
                DXGI_ADAPTER_DESC desc;
                if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
                    oss << L"|" << std::hex << desc.VendorId << L":" << desc.DeviceId << std::dec;
                }
                pAdapter->Release();
            }
            pFactory->Release();
        }
        return oss.str();
    }

    std::wstring BaseChroma(std::wstring chroma) {
        if (chroma.size() > 4 && chroma.compare(chroma.size() - 4, 4, L"10le") == 0) {
            chroma.erase(chroma.size() - 4);
        }
        if (chroma != L"yuv420p" && chroma != L"yuv422p" && chroma != L"yuv444p") {
            chroma = L"yuv420p";
        }
        return chroma;
    }

    bool Contains(const std::wstring& s, const wchar_t* part) {
        return s.find(part) != std::wstring::npos;
    }

    bool GpuSupports(const std::wstring& codec, const std::wstring& chroma, int depth) {
        bool nv = Contains(codec, L"nvenc");
        if (chroma == L"yuv422p") return false;
        if (Contains(codec, L"h264")) {
            if (depth == 10) return false;
            return chroma == L"yuv420p" || (chroma == L"yuv444p" && nv);
        }
        if (Contains(codec, L"hevc")) {
            return chroma == L"yuv420p" || (chroma == L"yuv444p" && nv);
        }
        return chroma == L"yuv420p";
    }

    std::wstring RateString(long long frameDuration, const MmdOutputInfo& mmd) {
        double fromDuration = frameDuration > 0 ? 10000000.0 / static_cast<double>(frameDuration) : 30.0;
        double fps = fromDuration;
        bool fromMmd = false;
        if (mmd.valid && mmd.fps > 0.0 && std::fabs(mmd.fps - fromDuration) <= fromDuration * 0.01) {
            fps = mmd.fps;
            fromMmd = true;
        }
        double rounded = std::floor(fps + 0.5);
        if (std::fabs(fps - rounded) < 0.001) {
            return std::to_wstring(static_cast<long long>(rounded));
        }
        struct NtscRate { double value; const wchar_t* text; };
        const NtscRate ntsc[] = {
            { 23.976, L"24000/1001" }, { 29.97, L"30000/1001" }, { 47.952, L"48000/1001" },
            { 59.94, L"60000/1001" }, { 119.88, L"120000/1001" }
        };
        for (const auto& r : ntsc) {
            if (std::fabs(fps - r.value) < 0.005) return r.text;
        }
        if (fromMmd) {
            wchar_t buf[32];
            swprintf_s(buf, L"%.6g", fps);
            return buf;
        }
        if (frameDuration > 0) {
            return L"10000000/" + std::to_wstring(frameDuration);
        }
        return L"30";
    }

    std::wstring FormatSequencePath(const std::wstring& base, int digits, long long number, const std::wstring& ext) {
        wchar_t fmt[16];
        swprintf_s(fmt, L"_%%0%dlld.", digits);
        wchar_t num[64];
        swprintf_s(num, fmt, number);
        return base + num + ext;
    }

    std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
        return s;
    }
}

EncoderCapabilities EncoderController::GetCachedCapabilities(const std::wstring& ffmpegPath, bool forceRefresh) {
    std::wstring ini = EncoderConfig::GetDefaultIniPath();
    std::wstring resolved = EncoderConfig::ResolveExecutable(L"ffmpeg", ffmpegPath);
    std::wstring signature = CapabilitySignature(resolved);
    if (!forceRefresh) {
        int cached = GetPrivateProfileIntW(L"hardware", L"cached", 0, ini.c_str());
        wchar_t sigBuf[2048] = {};
        GetPrivateProfileStringW(L"hardware", L"signature", L"", sigBuf, 2048, ini.c_str());
        if (cached == 1 && signature == sigBuf) {
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
    WritePrivateProfileStringW(L"hardware", L"signature", signature.c_str(), ini.c_str());
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

int EncoderController::DigitCount(long long value) {
    if (value < 0) value = -value;
    int digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

std::wstring EncoderController::Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0) {
        return std::wstring(s.begin(), s.end());
    }
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring EncoderController::BuildVideoArgs(const EncoderConfig& config,
                                               const std::wstring& codec,
                                               bool bottomUp) {
    std::vector<std::wstring> filters;
    if (bottomUp) {
        filters.push_back(L"vflip");
    }

    std::wstring effective = config.EffectiveFormat();
    std::wstring params;
    std::wstring pix;

    if (codec == L"png") {
        pix = config.alpha_enabled ? L"rgba" : L"rgb24";
        params = L" -c:v png";
    } else if (codec == L"mjpeg") {
        filters.push_back(L"scale=out_color_matrix=bt601:out_range=pc");
        filters.push_back(L"format=yuvj420p");
        pix = L"yuvj420p";
        params = L" -c:v mjpeg -q:v 2";
    } else {
        std::wstring chroma = BaseChroma(config.chroma);
        bool ten = config.bit_depth == 10;
        int quality = config.crf;

        if (codec == L"prores_ks") {
            int profile = 3;
            if (effective == L"prores422") profile = 2;
            else if (effective == L"prores4444") profile = 4;
            else if (effective == L"prores4444xq") profile = 5;
            pix = config.alpha_enabled ? L"yuva444p10le" : L"yuv422p10le";
            params = L" -c:v prores_ks -profile:v " + std::to_wstring(profile) + L" -vendor apl0";
        } else if (codec == L"libvpx-vp9") {
            pix = config.alpha_enabled ? L"yuva420p" : (ten ? chroma + L"10le" : chroma);
            params = L" -c:v libvpx-vp9 -crf " + std::to_wstring(quality) + L" -b:v 0 -deadline good -cpu-used 2 -row-mt 1";
        } else if (Contains(codec, L"nvenc")) {
            if (chroma == L"yuv444p") pix = ten ? L"yuv444p16le" : L"yuv444p";
            else pix = ten ? L"p010le" : L"yuv420p";
            if (quality == 0) {
                params = L" -c:v " + codec + L" -preset p5 -tune lossless";
            } else {
                params = L" -c:v " + codec + L" -preset p5 -rc vbr -cq " + std::to_wstring((std::min)(quality, 51)) + L" -b:v 0";
            }
            if (config.lookahead > 0) {
                params += L" -rc-lookahead " + std::to_wstring(config.lookahead);
            }
        } else if (Contains(codec, L"qsv")) {
            pix = ten ? L"p010le" : L"nv12";
            params = L" -c:v " + codec + L" -global_quality " + std::to_wstring((std::max)(1, quality));
        } else if (Contains(codec, L"amf")) {
            pix = ten ? L"p010le" : L"yuv420p";
            params = L" -c:v " + codec + L" -rc cqp -qp_i " + std::to_wstring(quality) + L" -qp_p " + std::to_wstring(quality);
        } else {
            pix = ten ? chroma + L"10le" : chroma;
            params = L" -c:v " + codec + L" -crf " + std::to_wstring(quality);
            if (codec == L"libx264" || codec == L"libx265") {
                params += L" -bf " + std::to_wstring(config.b_frames);
            }
        }

        std::wstring range = (config.color_range == L"pc") ? L"pc" : L"tv";
        std::wstring matrix = (config.colorspace == L"bt601") ? L"bt601" : L"bt709";
        filters.push_back(L"scale=out_color_matrix=" + matrix + L":out_range=" + range);
        filters.push_back(L"format=" + pix);

        if (config.colorspace == L"bt601") {
            params += L" -colorspace smpte170m -color_primaries smpte170m -color_trc smpte170m";
        } else {
            params += L" -colorspace bt709 -color_primaries bt709 -color_trc bt709";
        }
        params += L" -color_range " + range;

        if (codec != L"prores_ks" && !config.gop_auto && config.gop_size > 0) {
            params += L" -g " + std::to_wstring(config.gop_size);
        }
    }

    std::wstring out;
    if (!filters.empty()) {
        out += L" -vf \"";
        for (size_t i = 0; i < filters.size(); ++i) {
            if (i > 0) out += L",";
            out += filters[i];
        }
        out += L"\"";
    }
    out += params;
    out += L" -pix_fmt " + pix;
    return out;
}

std::wstring EncoderController::SequenceFramePath(const ExecutionPlan& plan, long long number) {
    return FormatSequencePath(plan.sequence_base, plan.sequence_digits, number, plan.output_ext);
}

ExecutionPlan EncoderController::PrepareExecutionPlan(const EncoderConfig& config,
                                                     int width,
                                                     int height,
                                                     long long frameDuration,
                                                     const std::wstring& inputPixFmt,
                                                     bool bottomUp,
                                                     const std::wstring& aviPath,
                                                     const MmdOutputInfo& mmd) {
    ExecutionPlan plan;
    std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", config.ffmpeg_path);
    std::wstring effective = config.EffectiveFormat();
    std::wstring chroma = BaseChroma(config.chroma);

    std::wstring targetCodec;
    std::wstring targetBackend = L"cpu";

    if (effective == L"png") {
        targetCodec = L"png";
    } else if (effective == L"jpg") {
        targetCodec = L"mjpeg";
    } else if (effective == L"exr") {
        targetCodec = L"exr";
        plan.native_exr = true;
    } else if (config.IsProRes()) {
        targetCodec = L"prores_ks";
    } else if (effective == L"vp9") {
        targetCodec = L"libvpx-vp9";
    } else {
        EncoderCapabilities caps;
        if (config.backend != L"cpu") {
            caps = GetCachedCapabilities(ffmpeg);
        }
        struct Candidate { const wchar_t* backend; bool available; const wchar_t* codec; };
        std::vector<Candidate> candidates;
        std::wstring cpuCodec;
        if (effective == L"hevc") {
            cpuCodec = L"libx265";
            candidates = { { L"nvidia", caps.nvenc_hevc, L"hevc_nvenc" }, { L"intel", caps.qsv_hevc, L"hevc_qsv" }, { L"amd", caps.amf_hevc, L"hevc_amf" } };
        } else if (effective == L"av1" || effective == L"av1webm") {
            cpuCodec = L"libsvtav1";
            candidates = { { L"nvidia", caps.nvenc_av1, L"av1_nvenc" }, { L"intel", caps.qsv_av1, L"av1_qsv" }, { L"amd", caps.amf_av1, L"av1_amf" } };
        } else {
            cpuCodec = L"libx264";
            candidates = { { L"nvidia", caps.nvenc_h264, L"h264_nvenc" }, { L"intel", caps.qsv_h264, L"h264_qsv" }, { L"amd", caps.amf_h264, L"h264_amf" } };
        }
        targetCodec = cpuCodec;
        if (config.backend == L"auto") {
            for (const auto& c : candidates) {
                if (c.available && GpuSupports(c.codec, chroma, config.bit_depth)) {
                    targetCodec = c.codec;
                    targetBackend = c.backend;
                    break;
                }
            }
        } else if (config.backend != L"cpu") {
            bool found = false;
            for (const auto& c : candidates) {
                if (config.backend == c.backend && c.available && GpuSupports(c.codec, chroma, config.bit_depth)) {
                    targetCodec = c.codec;
                    targetBackend = c.backend;
                    found = true;
                    break;
                }
            }
            if (!found) plan.fallback_occurred = true;
        }
    }

    plan.chosen_codec = targetCodec;
    plan.chosen_backend = targetBackend;
    plan.elementary_muxer = L"h264";
    plan.elementary_fourcc = GetFourcc('H', '2', '6', '4');
    plan.input_rate = RateString(frameDuration, mmd);

    plan.is_image_sequence = (effective == L"png" || effective == L"jpg" || effective == L"exr");
    if (plan.is_image_sequence) plan.output_ext = effective;
    else if (config.IsProRes()) plan.output_ext = L"mov";
    else if (effective == L"vp9" || effective == L"av1webm") plan.output_ext = L"webm";
    else plan.output_ext = L"mp4";

    if (plan.is_image_sequence) plan.audio_codec.clear();
    else if (plan.output_ext == L"mp4") plan.audio_codec = L"aac";
    else if (plan.output_ext == L"webm") plan.audio_codec = L"libopus";
    else plan.audio_codec = L"copy";

    std::wstring mainOutPath;
    if (!aviPath.empty()) {
        size_t dot = aviPath.find_last_of(L'.');
        size_t slash = aviPath.find_last_of(L"\\/");
        size_t cut = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) ? dot : aviPath.size();
        std::wstring base = aviPath.substr(0, cut);
        if (plan.is_image_sequence) {
            plan.sequence_base = base;
            if (mmd.valid) {
                plan.sequence_start = mmd.start_frame;
                plan.sequence_digits = DigitCount(mmd.end_frame);
                plan.sequence_renumber = false;
            } else {
                plan.sequence_start = 0;
                plan.sequence_digits = kTempSequenceDigits;
                plan.sequence_renumber = true;
            }
            mainOutPath = base + L"_%0" + std::to_wstring(plan.sequence_digits) + L"d." + plan.output_ext;
            plan.primary_output_path = SequenceFramePath(plan, plan.sequence_start);
        } else {
            mainOutPath = base + L"." + plan.output_ext;
            plan.primary_output_path = mainOutPath;
        }
    }

    std::wstring cmd;
    AppendQuoted(cmd, ffmpeg);
    cmd += L" -y -hide_banner -loglevel error -nostdin";
    if (Contains(targetCodec, L"qsv")) {
        cmd += L" -init_hw_device qsv=hw -filter_hw_device hw";
    }
    cmd += L" -f rawvideo -pix_fmt " + inputPixFmt;
    cmd += L" -s " + std::to_wstring(width) + L"x" + std::to_wstring(height);
    cmd += L" -r " + plan.input_rate;
    cmd += L" -i pipe:0";

    if (!mainOutPath.empty() && !plan.native_exr) {
        cmd += L" -map 0:v";
        cmd += BuildVideoArgs(config, targetCodec, bottomUp);
        if (Contains(targetCodec, L"hevc") || targetCodec == L"libx265") {
            cmd += L" -tag:v hvc1";
        }
        if (plan.is_image_sequence) {
            cmd += L" -start_number " + std::to_wstring(plan.sequence_start);
        }
        AppendQuoted(cmd, mainOutPath);
    }

    cmd += L" -map 0:v";
    if (bottomUp) {
        cmd += L" -vf vflip";
    }
    cmd += L" -c:v libx264 -preset ultrafast -pix_fmt yuv420p";
    if (config.delete_avi) {
        cmd += L" -crf 51";
    }
    cmd += L" -f h264 pipe:1";

    plan.main_command = cmd;
    return plan;
}

bool EncoderController::HasAudioStream(const std::wstring& ffmpegPath, const std::wstring& mediaPath) {
    if (mediaPath.empty() || !PathFileExistsDirect(mediaPath)) return false;
    std::wstring cmd;
    AppendQuoted(cmd, ffmpegPath);
    cmd += L" -hide_banner -nostdin -i";
    AppendQuoted(cmd, mediaPath);
    std::string info;
    ExecuteCommand(cmd, 60000, &info);
    return info.find("Audio:") != std::string::npos;
}

bool EncoderController::MergeAudio(const std::wstring& ffmpegPath,
                                   const std::wstring& targetVideoPath,
                                   const std::wstring& audioSourcePath,
                                   double audioOffsetSeconds,
                                   double durationSeconds,
                                   const std::wstring& audioCodec,
                                   std::string* outLog) {
    if (audioCodec.empty() || targetVideoPath.empty() || audioSourcePath.empty() ||
        !PathFileExistsDirect(targetVideoPath) || !PathFileExistsDirect(audioSourcePath)) {
        return false;
    }

    size_t dot = targetVideoPath.find_last_of(L'.');
    std::wstring tempMerged = (dot == std::wstring::npos) ? (targetVideoPath + L".tmp_merge") : (targetVideoPath.substr(0, dot) + L".tmp_merge" + targetVideoPath.substr(dot));

    std::wstring cmd;
    AppendQuoted(cmd, ffmpegPath);
    cmd += L" -y -hide_banner -loglevel error -nostdin -i";
    AppendQuoted(cmd, targetVideoPath);
    if (audioOffsetSeconds > 0.0) {
        wchar_t offset[64];
        swprintf_s(offset, L" -ss %.6f", audioOffsetSeconds);
        cmd += offset;
    }
    if (durationSeconds > 0.0) {
        wchar_t duration[64];
        swprintf_s(duration, L" -t %.6f", durationSeconds);
        cmd += duration;
    }
    cmd += L" -i";
    AppendQuoted(cmd, audioSourcePath);
    std::wstring codec = audioCodec;
    if (codec == L"copy" && durationSeconds > 0.0) {
        std::wstring probe;
        AppendQuoted(probe, ffmpegPath);
        probe += L" -hide_banner -nostdin -i";
        AppendQuoted(probe, audioSourcePath);
        std::string info;
        ExecuteCommand(probe, 60000, &info);
        size_t at = info.find("Audio: pcm_");
        if (at != std::string::npos) {
            size_t begin = at + 7;
            size_t end = info.find_first_of(" ,(\r\n", begin);
            codec = Utf8ToWide(info.substr(begin, end - begin));
        }
    }
    cmd += L" -map 0:v:0 -map 1:a:0 -map_chapters -1 -c:v copy -c:a " + codec;
    if (codec != L"copy" && codec.compare(0, 4, L"pcm_") != 0) {
        cmd += L" -b:a 192k";
    }
    if (durationSeconds <= 0.0) {
        cmd += L" -shortest";
    }
    AppendQuoted(cmd, tempMerged);

    bool ok = ExecuteCommand(cmd, 3600000, outLog);
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

bool EncoderController::RenumberSequence(ExecutionPlan& plan, long long frameCount) {
    if (!plan.is_image_sequence || !plan.sequence_renumber || frameCount <= 0) return true;
    long long last = plan.sequence_start + frameCount - 1;
    int digits = DigitCount(last);
    bool ok = true;
    for (long long i = 0; i < frameCount; ++i) {
        long long number = plan.sequence_start + i;
        std::wstring from = FormatSequencePath(plan.sequence_base, plan.sequence_digits, number, plan.output_ext);
        std::wstring to = FormatSequencePath(plan.sequence_base, digits, number, plan.output_ext);
        if (from == to || !PathFileExistsDirect(from)) continue;
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) ok = false;
    }
    if (ok) {
        plan.sequence_digits = digits;
        plan.sequence_renumber = false;
        plan.primary_output_path = FormatSequencePath(plan.sequence_base, digits, plan.sequence_start, plan.output_ext);
    }
    return ok;
}

bool EncoderController::RunEncoderTest(const EncoderConfig& config, std::wstring& outMessage) {
    int lang = config.ui_language;
    if (lang == 0) {
        LANGID sysLang = PRIMARYLANGID(GetUserDefaultUILanguage());
        if (sysLang == LANG_JAPANESE) lang = 1;
        else lang = 2;
    }

    std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", config.ffmpeg_path);
    if (!PathFileExistsDirect(ffmpeg) && ffmpeg != L"ffmpeg") {
        if (lang == 2) outMessage = L"ffmpeg.exe was not found.";
        else outMessage = L"ffmpeg.exe が見つかりません。";
        return false;
    }

    GetCachedCapabilities(config.ffmpeg_path, true);
    ExecutionPlan plan = PrepareExecutionPlan(config, 256, 256, 333333, L"bgra", false, L"test.avi");

    std::string err;
    bool ok = false;
    if (plan.native_exr) {
        std::vector<BYTE> frame(256 * 256 * 4);
        for (size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<BYTE>(i * 7);
        wchar_t tempDir[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring testPath = std::wstring(tempDir) + L"mmdirect_encoder_test.exr";
        ok = WriteExrFrame(testPath, frame.data(), 256, 256, 256 * 4, L"bgra", false, config.alpha_enabled);
        DeleteFileW(testPath.c_str());
        if (!ok) err = "EXR write failed";
    } else {
        std::wstring cmd;
        AppendQuoted(cmd, ffmpeg);
        cmd += L" -hide_banner -loglevel error -nostdin";
        if (Contains(plan.chosen_codec, L"qsv")) {
            cmd += L" -init_hw_device qsv=hw -filter_hw_device hw";
        }
        cmd += L" -f lavfi -i testsrc2=s=256x256:r=30:d=0.2,format=bgra";
        cmd += BuildVideoArgs(config, plan.chosen_codec, false);
        cmd += L" -frames:v 3 -f null -";
        ok = ExecuteCommand(cmd, 60000, &err);
    }

    if (ok) {
        if (lang == 2) {
            outMessage = L"Test Successful: " + plan.chosen_codec + L" is available.";
            if (plan.fallback_occurred) {
                outMessage += L" (The selected GPU cannot handle these settings, so the CPU encoder is used)";
            }
        } else {
            outMessage = L"テスト成功: " + plan.chosen_codec + L" は正常に利用可能です。";
            if (plan.fallback_occurred) {
                outMessage += L" (指定GPUではこの設定を扱えないため、CPUで処理します)";
            }
        }
        return true;
    }

    std::wstring wErr = Utf8ToWide(err);
    if (lang == 2) {
        outMessage = L"Test Failed: " + plan.chosen_codec + L"\n" + wErr;
    } else {
        outMessage = L"テスト失敗: " + plan.chosen_codec + L"\n" + wErr;
    }
    return false;
}

void EncoderController::WriteExportLog(const std::wstring& logDir,
                                       const ExecutionPlan& plan,
                                       const MmdOutputInfo& mmd,
                                       int width,
                                       int height,
                                       long long frameCount,
                                       DWORD exitCode,
                                       double elapsedSeconds,
                                       const std::wstring& result,
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

    std::wostringstream ofs;
    ofs << L"=== MMDirectEncoder Export Diagnostic Log ===\r\n";
    ofs << std::setfill(L'0') << L"Date: " << (tmNow.tm_year + 1900) << L"-" << std::setw(2) << (tmNow.tm_mon + 1) << L"-" << std::setw(2) << tmNow.tm_mday
        << L" " << std::setw(2) << tmNow.tm_hour << L":" << std::setw(2) << tmNow.tm_min << L":" << std::setw(2) << tmNow.tm_sec << L"\r\n";
    ofs << L"Result: " << result << L"\r\n";
    ofs << L"Chosen Codec: " << plan.chosen_codec << L"\r\n";
    ofs << L"Chosen Backend: " << plan.chosen_backend << L"\r\n";
    ofs << L"Fallback Occurred: " << (plan.fallback_occurred ? L"Yes" : L"No") << L"\r\n";
    ofs << L"Input Resolution: " << width << L"x" << height << L"\r\n";
    ofs << L"Input Rate: " << plan.input_rate << L"\r\n";
    if (mmd.valid) {
        ofs << L"MMD Output Range: " << mmd.start_frame << L" - " << mmd.end_frame << L" (" << mmd.fps << L" fps, WAVE " << (mmd.wave_enabled ? L"on" : L"off") << L")\r\n";
        ofs << L"MMD WAV: " << (mmd.wav_path.empty() ? std::wstring(L"none") : mmd.wav_path) << L"\r\n";
    } else {
        ofs << L"MMD Output Range: unavailable\r\n";
    }
    ofs << L"Processed Frames: " << frameCount << L"\r\n";
    ofs << L"Elapsed Time: " << elapsedSeconds << L" s\r\n";
    ofs << L"Exit Code: " << exitCode << L"\r\n";
    ofs << L"Output File: " << plan.primary_output_path << L"\r\n";

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!plan.primary_output_path.empty() && GetFileAttributesExW(plan.primary_output_path.c_str(), GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER sz;
        sz.HighPart = static_cast<LONG>(fad.nFileSizeHigh);
        sz.LowPart = fad.nFileSizeLow;
        ofs << L"Output File Size: " << (sz.QuadPart / 1024) << L" KB\r\n";
    }
    ofs << L"Full Command: " << plan.main_command << L"\r\n";

    std::string content = "\xEF\xBB\xBF" + WideToUtf8(ofs.str());
    if (!processStderr.empty()) {
        content += "Stderr Output:\r\n";
        content += processStderr;
        content += "\r\n";
    }

    for (const std::wstring& path : { nameOss.str(), logDir + L"\\latest.log" }) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        DWORD written = 0;
        WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, NULL);
        CloseHandle(h);
    }

    std::vector<std::wstring> logs;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((logDir + L"\\export_*.log").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) logs.push_back(fd.cFileName);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    const size_t kKeepLogs = 50;
    if (logs.size() > kKeepLogs) {
        std::sort(logs.begin(), logs.end());
        for (size_t i = 0; i < logs.size() - kKeepLogs; ++i) {
            DeleteFileW((logDir + L"\\" + logs[i]).c_str());
        }
    }
}
