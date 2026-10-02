#include "mmd_host.h"

#include <cmath>
#include <cwchar>

namespace {
    const size_t kGlobalSlotRva = 0x1445F8;
    const size_t kAviRangeOffset = 0xA1B24;
    const size_t kWavPathOffset = 0xD8;
    const size_t kWavPathChars = 260;

    bool Readable(const void* p, size_t n) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & 0xFF) == PAGE_NOACCESS) return false;
        const BYTE* regionEnd = static_cast<const BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
        return static_cast<const BYTE*>(p) + n <= regionEnd;
    }

    size_t ImageSize(HMODULE module) {
        const BYTE* base = reinterpret_cast<const BYTE*>(module);
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return nt->OptionalHeader.SizeOfImage;
    }
}

MmdOutputInfo ReadMmdOutputInfo(long long frameDuration) {
    MmdOutputInfo info;
    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) return info;

    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(exe, path, MAX_PATH) == 0) return info;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    if (_wcsicmp(name, L"MikuMikuDance.exe") != 0) return info;
    if (!GetProcAddress(exe, "ExpGetFrameTime")) return info;
    if (ImageSize(exe) < kGlobalSlotRva + sizeof(void*)) return info;

    const BYTE* base = reinterpret_cast<const BYTE*>(exe);
    const BYTE* const* slot = reinterpret_cast<const BYTE* const*>(base + kGlobalSlotRva);
    if (!Readable(slot, sizeof(void*))) return info;
    const BYTE* global = *slot;
    if (!global || !Readable(global + kAviRangeOffset, 16)) return info;

    int start = *reinterpret_cast<const int*>(global + kAviRangeOffset);
    int end = *reinterpret_cast<const int*>(global + kAviRangeOffset + 4);
    float fps = *reinterpret_cast<const float*>(global + kAviRangeOffset + 8);
    int wave = *reinterpret_cast<const int*>(global + kAviRangeOffset + 12);

    if (start < 0 || end < start || end > 10000000) return info;
    if (!(fps > 0.0f && fps <= 1000.0f)) return info;
    if (wave != 0 && wave != 1) return info;
    if (frameDuration > 0) {
        double expected = 10000000.0 / static_cast<double>(frameDuration);
        if (std::fabs(static_cast<double>(fps) - expected) > expected * 0.01) return info;
    }

    info.valid = true;
    info.start_frame = start;
    info.end_frame = end;
    info.fps = static_cast<double>(fps);
    info.wave_enabled = wave == 1;

    if (Readable(global + kWavPathOffset, kWavPathChars * sizeof(wchar_t))) {
        const wchar_t* wav = reinterpret_cast<const wchar_t*>(global + kWavPathOffset);
        size_t len = wcsnlen(wav, kWavPathChars);
        if (len > 4 && len < kWavPathChars && _wcsicmp(wav + len - 4, L".wav") == 0) {
            std::wstring candidate(wav, len);
            DWORD attr = GetFileAttributesW(candidate.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                info.wav_path = candidate;
            }
        }
    }
    return info;
}
