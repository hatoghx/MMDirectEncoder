#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include "resource.h"

static std::wstring GetExecutableDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring s(path);
    size_t pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return s.substr(0, pos);
    }
    return L"";
}

static bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static std::wstring ResolveSourceFile(const std::wstring& exeDir, const std::wstring& filename) {
    std::wstring c1 = exeDir + L"\\" + filename;
    if (FileExists(c1)) return c1;
    std::wstring c2 = exeDir + L"\\build\\" + filename;
    if (FileExists(c2)) return c2;
    std::wstring c3 = exeDir + L"\\..\\build\\" + filename;
    if (FileExists(c3)) return c3;
    std::wstring c4 = exeDir + L"\\code\\build\\" + filename;
    if (FileExists(c4)) return c4;
    return L"";
}

static std::wstring ResolveFfmpeg(const std::wstring& exeDir, const std::wstring& installDir) {
    std::wstring c1 = exeDir + L"\\bin\\ffmpeg.exe";
    if (FileExists(c1)) return c1;
    std::wstring c2 = exeDir + L"\\ffmpeg.exe";
    if (FileExists(c2)) return c2;
    std::wstring c3 = exeDir + L"\\..\\bin\\ffmpeg.exe";
    if (FileExists(c3)) return c3;
    std::wstring c4 = installDir + L"\\bin\\ffmpeg.exe";
    if (FileExists(c4)) return c4;

    wchar_t localApp[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localApp))) {
        std::wstring c5 = std::wstring(localApp) + L"\\Microsoft\\WinGet\\Packages\\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\\ffmpeg-7.1.1-full_build\\bin\\ffmpeg.exe";
        if (FileExists(c5)) return c5;
    }

    wchar_t searchBuf[MAX_PATH] = {};
    DWORD found = SearchPathW(NULL, L"ffmpeg.exe", NULL, MAX_PATH, searchBuf, NULL);
    if (found > 0 && found < MAX_PATH) {
        return std::wstring(searchBuf, found);
    }
    return L"";
}

static bool SetRegString(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName, const std::wstring& data) {
    HKEY hKey = NULL;
    LONG res = RegCreateKeyExW(hRoot, subKey.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (res != ERROR_SUCCESS) return false;
    const wchar_t* valNamePtr = valueName.empty() ? NULL : valueName.c_str();
    res = RegSetValueExW(hKey, valNamePtr, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

static bool SetRegDWORD(HKEY hRoot, const std::wstring& subKey, const std::wstring& valueName, DWORD data) {
    HKEY hKey = NULL;
    LONG res = RegCreateKeyExW(hRoot, subKey.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
    if (res != ERROR_SUCCESS) return false;
    const wchar_t* valNamePtr = valueName.empty() ? NULL : valueName.c_str();
    res = RegSetValueExW(hKey, valNamePtr, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(DWORD));
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

static bool CreateShellShortcut(const std::wstring& shortcutPath, const std::wstring& targetPath, const std::wstring& workDir) {
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetPath.c_str());
        if (!workDir.empty()) {
            psl->SetWorkingDirectory(workDir.c_str());
        }
        IPersistFile* ppf = NULL;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(shortcutPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    return SUCCEEDED(hr);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    (void)hInstance;

    bool silent = false;
    if (lpCmdLine && (wcsstr(lpCmdLine, L"/s") != NULL || wcsstr(lpCmdLine, L"/silent") != NULL || wcsstr(lpCmdLine, L"-s") != NULL)) {
        silent = true;
    }

    LANGID sysLang = PRIMARYLANGID(GetUserDefaultUILanguage());
    int lang = (sysLang == LANG_JAPANESE) ? 1 : ((sysLang == LANG_CHINESE) ? 3 : 2);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MMDirectInstallerOwner";
    wc.hIcon = reinterpret_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    RegisterClassExW(&wc);
    HWND hOwner = CreateWindowExW(0, L"MMDirectInstallerOwner", L"MMDirect Encoder", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    wchar_t localAppData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        if (hOwner) DestroyWindow(hOwner);
        return 1;
    }
    std::wstring installDir = std::wstring(localAppData) + L"\\MMDirect Encoder";
    std::wstring binDir = installDir + L"\\bin";

    if (!silent) {
        std::wstring prompt;
        std::wstring title;
        if (lang == 3) {
            prompt = L"是否安装 MMDirect Encoder？\n\n安装路径:\n" + installDir;
            title = L"MMDirect Encoder 安装";
        } else if (lang == 1) {
            prompt = L"MMDirect Encoder をインストールしますか？\n\nインストール先:\n" + installDir;
            title = L"MMDirect Encoder インストール";
        } else {
            prompt = L"Do you want to install MMDirect Encoder?\n\nDestination:\n" + installDir;
            title = L"MMDirect Encoder Setup";
        }

        int r = MessageBoxW(hOwner, prompt.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) {
            if (hOwner) DestroyWindow(hOwner);
            return 0;
        }
    }

    std::wstring exeDir = GetExecutableDir();
    std::wstring srcDll = ResolveSourceFile(exeDir, L"MMDirectEncoder.dll");
    std::wstring srcCfg = ResolveSourceFile(exeDir, L"MMDirectEncoderConfig.exe");
    std::wstring srcUninst = ResolveSourceFile(exeDir, L"uninstall.exe");
    std::wstring srcInstall = ResolveSourceFile(exeDir, L"install.exe");
    std::wstring srcFfmpeg = ResolveFfmpeg(exeDir, installDir);

    if (srcDll.empty() || srcCfg.empty()) {
        if (!silent) {
            std::wstring err;
            std::wstring title;
            if (lang == 3) {
                err = L"未找到安装所需的文件。";
                title = L"错误";
            } else if (lang == 1) {
                err = L"インストールに必要なファイルが見つかりませんでした。";
                title = L"エラー";
            } else {
                err = L"Required installation files were not found.";
                title = L"Error";
            }
            MessageBoxW(hOwner, err.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
        }
        if (hOwner) DestroyWindow(hOwner);
        return 1;
    }

    CreateDirectoryW(installDir.c_str(), NULL);
    CreateDirectoryW(binDir.c_str(), NULL);

    std::wstring targetDll = installDir + L"\\MMDirectEncoder.dll";
    std::wstring targetCfg = installDir + L"\\MMDirectEncoderConfig.exe";
    std::wstring targetUninst = installDir + L"\\uninstall.exe";
    std::wstring targetInstall = installDir + L"\\install.exe";
    std::wstring targetFfmpeg = binDir + L"\\ffmpeg.exe";

    CopyFileW(srcDll.c_str(), targetDll.c_str(), FALSE);
    CopyFileW(srcCfg.c_str(), targetCfg.c_str(), FALSE);
    if (!srcUninst.empty()) {
        CopyFileW(srcUninst.c_str(), targetUninst.c_str(), FALSE);
    }
    if (!srcInstall.empty() && _wcsicmp(srcInstall.c_str(), targetInstall.c_str()) != 0) {
        CopyFileW(srcInstall.c_str(), targetInstall.c_str(), FALSE);
    }
    if (!srcFfmpeg.empty()) {
        CopyFileW(srcFfmpeg.c_str(), targetFfmpeg.c_str(), FALSE);
    }

    std::wstring iniPath = installDir + L"\\MMDirectEncoder.ini";
    if (!FileExists(iniPath)) {
        HANDLE hFile = CreateFileW(iniPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            std::string defaultIni =
                "[general]\r\n"
                "preset=0\r\n"
                "language=0\r\n\r\n"
                "[video]\r\n"
                "format=h264\r\n"
                "backend=auto\r\n"
                "crf=18\r\n"
                "audio_enabled=1\r\n"
                "alpha_enabled=0\r\n"
                "alpha_format=prores\r\n\r\n"
                "[advanced]\r\n"
                "bit_depth=8\r\n"
                "chroma=yuv420p\r\n"
                "colorspace=bt709\r\n"
                "color_range=tv\r\n"
                "gop_auto=1\r\n"
                "gop_size=250\r\n"
                "b_frames=3\r\n"
                "lookahead=0\r\n"
                "extra_args=\r\n\r\n"
                "[output]\r\n"
                "container=mp4\r\n"
                "delete_avi=1\r\n"
                "merge_audio=1\r\n\r\n"
                "[paths]\r\n"
                "ffmpeg=\r\n";
            DWORD written = 0;
            WriteFile(hFile, defaultIni.data(), static_cast<DWORD>(defaultIni.size()), &written, NULL);
            CloseHandle(hFile);
        }
    }

    const std::wstring clsid = L"{D79D43B2-F005-40A4-BE18-AFD19C03E6E6}";
    const std::wstring filterName = L"MMDirect Encoder";
    const std::wstring catVideo = L"{33d9a760-90c8-11d0-bd43-00a0c911ce86}";
    const std::wstring catDirectShow = L"{083863F1-70DE-11d0-BD40-00A0C911CE86}";

    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid, L"", filterName);
    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", L"", targetDll);
    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", L"ThreadingModel", L"Both");

    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + catVideo + L"\\Instance\\" + clsid, L"CLSID", clsid);
    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + catVideo + L"\\Instance\\" + clsid, L"FriendlyName", filterName);

    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + catDirectShow + L"\\Instance\\" + clsid, L"CLSID", clsid);
    SetRegString(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + catDirectShow + L"\\Instance\\" + clsid, L"FriendlyName", filterName);

    std::wstring regCmd = L"/s \"" + targetDll + L"\"";
    ShellExecuteW(NULL, L"open", L"regsvr32.exe", regCmd.c_str(), NULL, SW_HIDE);

    const std::wstring uninstKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MMDirectEncoder";
    SetRegString(HKEY_CURRENT_USER, uninstKey, L"DisplayName", filterName);
    SetRegString(HKEY_CURRENT_USER, uninstKey, L"DisplayVersion", L"1.2.0");
    SetRegString(HKEY_CURRENT_USER, uninstKey, L"DisplayIcon", targetCfg);
    SetRegString(HKEY_CURRENT_USER, uninstKey, L"UninstallString", L"\"" + targetUninst + L"\"");
    SetRegDWORD(HKEY_CURRENT_USER, uninstKey, L"NoModify", 1);
    SetRegDWORD(HKEY_CURRENT_USER, uninstKey, L"NoRepair", 1);

    CoInitialize(NULL);
    wchar_t programsDir[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, programsDir))) {
        std::wstring smDir = std::wstring(programsDir) + L"\\MMDirect Encoder";
        CreateDirectoryW(smDir.c_str(), NULL);
        CreateShellShortcut(smDir + L"\\MMDirect Encoder Settings.lnk", targetCfg, installDir);
        if (FileExists(targetUninst)) {
            CreateShellShortcut(smDir + L"\\Uninstall.lnk", targetUninst, installDir);
        }

        std::wstring oldSmDir = std::wstring(programsDir) + L"\\MMD FFmpeg Encoder";
        DeleteFileW((oldSmDir + L"\\MMD FFmpeg Encoder Settings.lnk").c_str());
        DeleteFileW((oldSmDir + L"\\Uninstall.lnk").c_str());
        RemoveDirectoryW(oldSmDir.c_str());
    }
    CoUninitialize();

    if (!silent) {
        std::wstring succ;
        std::wstring title;
        if (lang == 3) {
            succ = L"MMDirect Encoder 安装已完成。";
            title = L"安装完成";
        } else if (lang == 1) {
            succ = L"MMDirect Encoder のインストールが完了しました。";
            title = L"インストール完了";
        } else {
            succ = L"MMDirect Encoder has been installed successfully.";
            title = L"Installation Complete";
        }
        MessageBoxW(hOwner, succ.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    if (hOwner) DestroyWindow(hOwner);
    return 0;
}
