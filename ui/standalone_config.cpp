#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <string>
#include "property_dialog.h"
#include "../config/encoder_config.h"
#include "../encoder/encoder_controller.h"

HINSTANCE g_hInst = NULL;

namespace {
    const CLSID kFilterClsid = { 0xD79D43B2, 0xF005, 0x40A4, { 0xBE, 0x18, 0xAF, 0xD1, 0x9C, 0x03, 0xE6, 0xE6 } };

    enum SelfTestResult {
        SelfTestOk = 0,
        SelfTestNotRegistered = 10,
        SelfTestFilterLoadFailed = 11,
        SelfTestFfmpegMissing = 20,
        SelfTestFfmpegNotRunnable = 21,
        SelfTestVideoEncodeFailed = 30,
        SelfTestExrFailed = 31
    };

    void WriteSelfTestLog(const std::wstring& text) {
        std::wstring path = EncoderConfig::GetLogDirectoryPath() + L"\\selftest.log";
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
        int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
        std::string utf8(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &utf8[0], n, NULL, NULL);
        DWORD written = 0;
        WriteFile(h, "\xEF\xBB\xBF", 3, &written, NULL);
        WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, NULL);
        CloseHandle(h);
    }

    int RunSelfTest() {
        std::wstring log = L"MMDirectEncoder self test\r\n";
        int result = SelfTestOk;

        HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        IUnknown* filter = NULL;
        HRESULT hr = CoCreateInstance(kFilterClsid, NULL, CLSCTX_INPROC_SERVER, IID_IUnknown, reinterpret_cast<void**>(&filter));
        wchar_t hrText[32];
        swprintf_s(hrText, L"0x%08X", static_cast<unsigned>(hr));
        log += L"Filter registration: " + std::wstring(hrText) + L"\r\n";
        if (filter) filter->Release();
        if (SUCCEEDED(hrInit)) CoUninitialize();
        if (hr == REGDB_E_CLASSNOTREG) {
            result = SelfTestNotRegistered;
        } else if (FAILED(hr)) {
            result = SelfTestFilterLoadFailed;
        }

        EncoderConfig config;
        config.Load();
        std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", config.ffmpeg_path);
        log += L"ffmpeg: " + ffmpeg + L"\r\n";
        DWORD attr = GetFileAttributesW(ffmpeg.c_str());
        bool ffmpegExists = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        if (result == SelfTestOk && !ffmpegExists) {
            result = SelfTestFfmpegMissing;
        }

        if (result == SelfTestOk) {
            std::wstring cmd = L"\"" + ffmpeg + L"\" -hide_banner -nostdin -version";
            std::string out;
            if (!EncoderController::ExecuteCommand(cmd, 30000, &out)) {
                log += L"ffmpeg -version failed\r\n";
                result = SelfTestFfmpegNotRunnable;
            }
        }

        if (result == SelfTestOk) {
            EncoderConfig video;
            video.ui_language = 2;
            video.ffmpeg_path = config.ffmpeg_path;
            video.ApplyPreset(PresetType::HighQualityH264);
            std::wstring message;
            bool ok = EncoderController::RunEncoderTest(video, message);
            log += L"Video test: " + message + L"\r\n";
            if (!ok) result = SelfTestVideoEncodeFailed;
        }

        if (result == SelfTestOk) {
            EncoderConfig exr;
            exr.ui_language = 2;
            exr.ffmpeg_path = config.ffmpeg_path;
            exr.format = L"exr";
            exr.ValidateAndCorrect();
            std::wstring message;
            bool ok = EncoderController::RunEncoderTest(exr, message);
            log += L"EXR test: " + message + L"\r\n";
            if (!ok) result = SelfTestExrFailed;
        }

        log += L"Result code: " + std::to_wstring(result) + L"\r\n";
        WriteSelfTestLog(log);
        return result;
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInst = hInstance;

    if (lpCmdLine && wcsstr(lpCmdLine, L"--selftest")) {
        return RunSelfTest();
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TAB_CLASSES | ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&icex);

    int initialTab = 0;
    if (lpCmdLine) {
        if (wcsstr(lpCmdLine, L"--tab 1")) {
            initialTab = 1;
        } else if (wcsstr(lpCmdLine, L"--tab 2")) {
            initialTab = 2;
        }
    }

    EncoderConfig config;
    config.Load();
    ShowEncoderSettingsDialog(NULL, config, initialTab);
    return 0;
}
