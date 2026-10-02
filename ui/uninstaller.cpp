#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include "resource.h"

static void DeleteKeyTree(HKEY hRoot, const std::wstring& subKey) {
    RegDeleteTreeW(hRoot, subKey.c_str());
}

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
    wc.lpszClassName = L"MMDirectUninstallerOwner";
    wc.hIcon = reinterpret_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    RegisterClassExW(&wc);
    HWND hOwner = CreateWindowExW(0, L"MMDirectUninstallerOwner", L"MMDirect Encoder", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    if (!silent) {
        std::wstring prompt;
        std::wstring title;
        if (lang == 3) {
            prompt = L"是否卸载 MMDirect Encoder？";
            title = L"MMDirect Encoder 卸载";
        } else if (lang == 1) {
            prompt = L"MMDirect Encoder をアンインストールしますか？";
            title = L"MMDirect Encoder アンインストール";
        } else {
            prompt = L"Do you want to uninstall MMDirect Encoder?";
            title = L"MMDirect Encoder Uninstall";
        }
        int r = MessageBoxW(hOwner, prompt.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (r != IDYES) {
            if (hOwner) DestroyWindow(hOwner);
            return 0;
        }
    }

    const std::wstring clsid = L"{D79D43B2-F005-40A4-BE18-AFD19C03E6E6}";
    const std::wstring catVideo = L"{33d9a760-90c8-11d0-bd43-00a0c911ce86}";
    const std::wstring catDirectShow = L"{083863F1-70DE-11d0-BD40-00A0C911CE86}";

    DeleteKeyTree(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + catVideo + L"\\Instance\\" + clsid);
    DeleteKeyTree(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + catDirectShow + L"\\Instance\\" + clsid);
    DeleteKeyTree(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid);
    DeleteKeyTree(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MMDirectEncoder");

    wchar_t localAppData[MAX_PATH] = {};
    std::wstring installDir;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        installDir = std::wstring(localAppData) + L"\\MMDirect Encoder";
    } else {
        installDir = GetExecutableDir();
    }

    std::wstring targetDll = installDir + L"\\MMDirectEncoder.dll";
    if (GetFileAttributesW(targetDll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        targetDll = GetExecutableDir() + L"\\MMDirectEncoder.dll";
    }

    if (GetFileAttributesW(targetDll.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring regCmd = L"/u /s \"" + targetDll + L"\"";
        ShellExecuteW(NULL, L"open", L"regsvr32.exe", regCmd.c_str(), NULL, SW_HIDE);
        Sleep(200);
    }

    wchar_t appData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, appData))) {
        std::wstring smDir = std::wstring(appData) + L"\\MMDirect Encoder";
        std::wstring oldSmDir = std::wstring(appData) + L"\\MMD FFmpeg Encoder";
        DeleteFileW((smDir + L"\\MMDirect Encoder Settings.lnk").c_str());
        DeleteFileW((smDir + L"\\Uninstall.lnk").c_str());
        RemoveDirectoryW(smDir.c_str());

        DeleteFileW((oldSmDir + L"\\MMD FFmpeg Encoder Settings.lnk").c_str());
        DeleteFileW((oldSmDir + L"\\Uninstall.lnk").c_str());
        RemoveDirectoryW(oldSmDir.c_str());
    }

    auto CleanDirectory = [](const std::wstring& dir) {
        DeleteFileW((dir + L"\\MMDirectEncoder.dll").c_str());
        DeleteFileW((dir + L"\\MMDirectEncoderConfig.exe").c_str());
        DeleteFileW((dir + L"\\MMDEncoderConfig.exe").c_str());
        DeleteFileW((dir + L"\\MMDirectEncoder.ini").c_str());
        DeleteFileW((dir + L"\\uninstall.cmd").c_str());
        DeleteFileW((dir + L"\\uninstall.ps1").c_str());
        DeleteFileW((dir + L"\\bin\\ffmpeg.exe").c_str());
        RemoveDirectoryW((dir + L"\\bin").c_str());

        WIN32_FIND_DATAW ffd;
        HANDLE hFind = FindFirstFileW((dir + L"\\logs\\*").c_str(), &ffd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    DeleteFileW((dir + L"\\logs\\" + ffd.cFileName).c_str());
                }
            } while (FindNextFileW(hFind, &ffd));
            FindClose(hFind);
            RemoveDirectoryW((dir + L"\\logs").c_str());
        }
    };

    CleanDirectory(installDir);

    if (!silent) {
        std::wstring succ;
        std::wstring title;
        if (lang == 3) {
            succ = L"MMDirect Encoder 卸载已完成。";
            title = L"卸载完成";
        } else if (lang == 1) {
            succ = L"MMDirect Encoder のアンインストールが完了しました。";
            title = L"アンインストール完了";
        } else {
            succ = L"MMDirect Encoder has been uninstalled successfully.";
            title = L"Uninstall Complete";
        }
        MessageBoxW(hOwner, succ.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    if (hOwner) DestroyWindow(hOwner);
    return 0;
}
