#include "ffmpeg_encoder.h"
#include "mmd_host.h"
#include "../../config/app_version.h"
#include "../../config/win_util.h"
#include "../../encoder/exr_writer.h"
#include "../../ui/property_dialog.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <utility>

extern HINSTANCE g_hInst;

namespace {
    const DWORD kStopWaitMs = 60000;
    const ULONGLONG kFinishWaitMs = 1800000;
    const DWORD kH264Fourcc = MAKEFOURCC('H', '2', '6', '4');

    const GUID kMmdRgb32 = {
        0x773C9AC0, 0x3274, 0x11D0,
        {0xB7, 0x24, 0x00, 0xAA, 0x00, 0x6C, 0x1A, 0x01}};

    bool SafeDeleteFileWithRetry(const std::wstring& path, int maxRetries = 25, DWORD delayMs = 100) {
        if (path.empty()) return true;
        for (int i = 0; i < maxRetries; i++) {
            if (!winutil::FileExists(path)) return true;
            if (DeleteFileW(path.c_str())) return true;
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
            Sleep(delayMs);
        }
        return !winutil::FileExists(path);
    }
}

DWORD WINAPI CFFmpegEncoder::StderrThreadProc(LPVOID param) {
    CFFmpegEncoder* pThis = reinterpret_cast<CFFmpegEncoder*>(param);
    char buf[4096];
    DWORD rd = 0;
    while (ReadFile(pThis->m_hChildStderrR, buf, sizeof(buf), &rd, NULL) && rd > 0) {
        CAutoLock lock(&pThis->m_stderrLock);
        std::string& out = pThis->m_stderrBuffer;
        if (out.size() + rd > EncoderController::kMaxCapturedOutput) {
            out.erase(0, (std::min)(out.size(), out.size() + rd - EncoderController::kMaxCapturedOutput / 2));
        }
        out.append(buf, rd);
    }
    return 0;
}

CUnknown* WINAPI CFFmpegEncoder::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
    return new CFFmpegEncoder(pUnk, phr);
}

CFFmpegEncoder::CFFmpegEncoder(LPUNKNOWN pUnk, HRESULT* phr)
    : CTransformFilter(NAME("MMDirectEncoder"), pUnk, CLSID_FFmpegEncoder)
{
    m_config.Load();
    if (phr) {
        *phr = S_OK;
    }
}

CFFmpegEncoder::~CFFmpegEncoder() {
    StopFFmpeg();
    if (m_aviDeletePending && !m_aviPath.empty()) {
        SafeDeleteFileWithRetry(m_aviPath, 5, 50);
    }
}

STDMETHODIMP CFFmpegEncoder::NonDelegatingQueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (riid == IID_IAMVfwCompressDialogs) {
        return GetInterface(static_cast<IAMVfwCompressDialogs*>(this), ppv);
    }
    if (riid == IID_ISpecifyPropertyPages) {
        return GetInterface(static_cast<ISpecifyPropertyPages*>(this), ppv);
    }
    return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CFFmpegEncoder::ShowDialog(int iDialog, HWND hwnd) {
    if (iDialog == VfwCompressDialog_QueryConfig) return S_OK;
    if (iDialog == VfwCompressDialog_QueryAbout) return S_OK;
    if (iDialog == VfwCompressDialog_About) {
        MessageBoxW(hwnd, L"MMDirectEncoder " MMDIRECT_VERSION_WSTRING, L"MMDirectEncoder", MB_OK | MB_ICONINFORMATION);
        return S_OK;
    }
    if (iDialog != VfwCompressDialog_Config) return E_INVALIDARG;

    m_config.Load();
    INT_PTR res = ShowEncoderSettingsDialog(hwnd, m_config);
    if (res == IDOK) {
        m_config.Save();
        return S_OK;
    }
    return S_FALSE;
}

STDMETHODIMP CFFmpegEncoder::GetState(LPVOID, int*) {
    return E_NOTIMPL;
}

STDMETHODIMP CFFmpegEncoder::SetState(LPVOID, int) {
    return E_NOTIMPL;
}

STDMETHODIMP CFFmpegEncoder::SendDriverMessage(int, long, long) {
    return E_NOTIMPL;
}

STDMETHODIMP CFFmpegEncoder::GetPages(CAUUID* pPages) {
    if (!pPages) return E_POINTER;
    pPages->cElems = 0;
    pPages->pElems = NULL;
    return E_NOTIMPL;
}

GUID CFFmpegEncoder::FourccGuid(DWORD fcc) {
    GUID g;
    g.Data1 = fcc;
    g.Data2 = 0x0000;
    g.Data3 = 0x0010;
    g.Data4[0] = 0x80; g.Data4[1] = 0x00; g.Data4[2] = 0x00;
    g.Data4[3] = 0xAA; g.Data4[4] = 0x00; g.Data4[5] = 0x38;
    g.Data4[6] = 0x9B; g.Data4[7] = 0x71;
    return g;
}

HRESULT CFFmpegEncoder::CheckInputType(const CMediaType* mtIn) {
    if (!mtIn) return E_POINTER;
    if (mtIn->majortype != MEDIATYPE_Video) return VFW_E_TYPE_NOT_ACCEPTED;

    if (!(IsEqualGUID(*mtIn->Subtype(), MEDIASUBTYPE_RGB24) ||
          IsEqualGUID(*mtIn->Subtype(), MEDIASUBTYPE_RGB32) ||
          IsEqualGUID(*mtIn->Subtype(), MEDIASUBTYPE_ARGB32) ||
          IsEqualGUID(*mtIn->Subtype(), MEDIASUBTYPE_RGB565) ||
          IsEqualGUID(*mtIn->Subtype(), MEDIASUBTYPE_RGB555) ||
          IsEqualGUID(*mtIn->Subtype(), kMmdRgb32))) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if ((*mtIn->FormatType() == FORMAT_VideoInfo && mtIn->FormatLength() < sizeof(VIDEOINFOHEADER)) ||
        (*mtIn->FormatType() == FORMAT_VideoInfo2 && mtIn->FormatLength() < sizeof(VIDEOINFOHEADER2)) ||
        (*mtIn->FormatType() != FORMAT_VideoInfo && *mtIn->FormatType() != FORMAT_VideoInfo2) ||
        !mtIn->Format()) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    return S_OK;
}

HRESULT CFFmpegEncoder::CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut) {
    if (!mtOut) return E_POINTER;
    HRESULT hr = CheckInputType(mtIn);
    if (FAILED(hr)) return hr;

    if (mtOut->majortype != MEDIATYPE_Video) return VFW_E_TYPE_NOT_ACCEPTED;

    GUID expected = FourccGuid(kH264Fourcc);
    if (!IsEqualGUID(*mtOut->Subtype(), expected)) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    return S_OK;
}

HRESULT CFFmpegEncoder::GetMediaType(int iPosition, CMediaType* pmt) {
    if (!pmt) return E_POINTER;
    if (iPosition < 0) return E_INVALIDARG;
    if (iPosition > 0) return VFW_S_NO_MORE_ITEMS;
    if (!m_pInput->IsConnected() || m_width <= 0 || m_height <= 0) return VFW_E_NOT_CONNECTED;

    VIDEOINFOHEADER vih;
    ZeroMemory(&vih, sizeof(vih));
    vih.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih.bmiHeader.biWidth = m_width;
    vih.bmiHeader.biHeight = m_height;
    vih.bmiHeader.biPlanes = 1;
    vih.bmiHeader.biBitCount = 24;
    vih.bmiHeader.biCompression = kH264Fourcc;
    vih.bmiHeader.biSizeImage = 0;
    vih.AvgTimePerFrame = m_frameDur;
    SetRect(&vih.rcSource, 0, 0, m_width, m_height);
    vih.rcTarget = vih.rcSource;

    GUID sub = FourccGuid(kH264Fourcc);
    pmt->InitMediaType();
    pmt->SetType(&MEDIATYPE_Video);
    pmt->SetSubtype(&sub);
    pmt->SetFormatType(&FORMAT_VideoInfo);
    pmt->SetTemporalCompression(TRUE);
    pmt->SetSampleSize(0);
    if (!pmt->SetFormat(reinterpret_cast<BYTE*>(&vih), sizeof(vih))) {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

HRESULT CFFmpegEncoder::SetMediaType(PIN_DIRECTION direction, const CMediaType* pmt) {
    if (!pmt) return E_POINTER;
    if (direction == PINDIR_INPUT) {
        HRESULT check = CheckInputType(pmt);
        if (FAILED(check)) return check;

        if (*pmt->FormatType() == FORMAT_VideoInfo) {
            VIDEOINFOHEADER* vih = reinterpret_cast<VIDEOINFOHEADER*>(pmt->Format());
            m_width = vih->bmiHeader.biWidth;
            if (vih->bmiHeader.biHeight == LONG_MIN) return VFW_E_INVALIDMEDIATYPE;
            m_height = abs(vih->bmiHeader.biHeight);
            m_bottomUp = vih->bmiHeader.biHeight > 0;
            m_frameDur = vih->AvgTimePerFrame > 0 ? vih->AvgTimePerFrame : m_frameDur;
        } else if (*pmt->FormatType() == FORMAT_VideoInfo2) {
            VIDEOINFOHEADER2* vih2 = reinterpret_cast<VIDEOINFOHEADER2*>(pmt->Format());
            m_width = vih2->bmiHeader.biWidth;
            if (vih2->bmiHeader.biHeight == LONG_MIN) return VFW_E_INVALIDMEDIATYPE;
            m_height = abs(vih2->bmiHeader.biHeight);
            m_bottomUp = vih2->bmiHeader.biHeight > 0;
            m_frameDur = vih2->AvgTimePerFrame > 0 ? vih2->AvgTimePerFrame : m_frameDur;
        } else {
            return VFW_E_INVALIDMEDIATYPE;
        }

        if (m_width <= 0 || m_height <= 0 || m_width > 16384 || m_height > 16384) {
            return VFW_E_INVALIDMEDIATYPE;
        }
        m_inSubtype = *pmt->Subtype();
        if (IsEqualGUID(m_inSubtype, MEDIASUBTYPE_RGB24)) {
            m_pixfmt = L"bgr24"; m_bpp = 24;
        } else if (IsEqualGUID(m_inSubtype, MEDIASUBTYPE_RGB32) ||
                   IsEqualGUID(m_inSubtype, MEDIASUBTYPE_ARGB32) ||
                   IsEqualGUID(m_inSubtype, kMmdRgb32)) {
            m_pixfmt = L"bgra"; m_bpp = 32;
        } else if (IsEqualGUID(m_inSubtype, MEDIASUBTYPE_RGB565)) {
            m_pixfmt = L"rgb565le"; m_bpp = 16;
        } else if (IsEqualGUID(m_inSubtype, MEDIASUBTYPE_RGB555)) {
            m_pixfmt = L"rgb555le"; m_bpp = 16;
        } else {
            return VFW_E_INVALIDMEDIATYPE;
        }
        m_rowBytes = m_width * (m_bpp / 8);
        m_stride = ((m_width * m_bpp + 31) / 32) * 4;
    }
    return CTransformFilter::SetMediaType(direction, pmt);
}

HRESULT CFFmpegEncoder::DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) {
    if (!pAlloc || !pProps) return E_POINTER;
    long cb = 65536;
    if (m_pInput->IsConnected()) {
        long sampleSize = m_pInput->CurrentMediaType().GetSampleSize();
        if (sampleSize > 0) cb = sampleSize;
    }
    if (m_width > 0 && m_height > 0 && m_bpp > 0) {
        LONGLONG raw = static_cast<LONGLONG>(m_width) * m_height * (m_bpp / 8);
        if (raw > cb && raw <= LONG_MAX) {
            cb = static_cast<long>(raw);
        }
    }
    pProps->cbBuffer = cb;
    pProps->cBuffers = 1;
    pProps->cbAlign = 1;
    pProps->cbPrefix = 0;

    ALLOCATOR_PROPERTIES actual;
    HRESULT hr = pAlloc->SetProperties(pProps, &actual);
    if (FAILED(hr)) return hr;
    if (actual.cbBuffer < pProps->cbBuffer) return E_FAIL;
    return S_OK;
}

void CFFmpegEncoder::ResolveOutputPaths() {
    m_aviPath.clear();
    if (m_pGraph) {
        IEnumFilters* enumerator = NULL;
        if (SUCCEEDED(m_pGraph->EnumFilters(&enumerator)) && enumerator) {
            IBaseFilter* filter = NULL;
            while (enumerator->Next(1, &filter, NULL) == S_OK) {
                IFileSinkFilter* sink = NULL;
                if (SUCCEEDED(filter->QueryInterface(IID_IFileSinkFilter, reinterpret_cast<void**>(&sink))) && sink) {
                    LPOLESTR path = NULL;
                    if (SUCCEEDED(sink->GetCurFile(&path, NULL)) && path) {
                        m_aviPath = path;
                        CoTaskMemFree(path);
                        sink->Release();
                        filter->Release();
                        break;
                    }
                    sink->Release();
                }
                filter->Release();
                filter = NULL;
            }
            enumerator->Release();
        }
    }
}

HRESULT CFFmpegEncoder::StartStreaming() {
    StopFFmpeg();

    CAutoLock lock(&m_lock);
    m_pktQueue.clear();
    m_tsQueue.clear();
    m_inBuf.clear();
    m_group.clear();
    m_groupHasVcl = false;
    m_lastTs = 0;
    m_firstPkt = true;
    m_readerDone = false;
    m_completed = false;
    m_eosReceived = false;
    m_alphaIgnored = false;
    m_nativeFailed = false;
    m_pipeFailed = false;
    m_ffmpegMissing = false;
    m_aviDeletePending = false;
    m_framesReceived = 0;
    m_stderrBuffer.clear();
    m_plan = ExecutionPlan();
    m_streamStartTime = std::chrono::steady_clock::now();

    m_config.Load();
    ResolveOutputPaths();
    m_mmd = ReadMmdOutputInfo(m_frameDur);
    m_launchPending = true;
    m_started = true;
    return S_OK;
}

HRESULT CFFmpegEncoder::StopStreaming() {
    if (!m_started) return S_OK;
    bool aborted = !m_eosReceived;
    StopFFmpeg();
    PostProcessOutputs(aborted);
    if (!m_aviPath.empty() && !SafeDeleteFileWithRetry(m_aviPath)) {
        m_aviDeletePending = true;
    }
    m_started = false;
    m_launchPending = false;
    return S_OK;
}

HRESULT CFFmpegEncoder::StartFFmpeg() {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };

    HANDLE hStdinRd = NULL, hStdinWr = NULL;
    HANDLE hStdoutRd = NULL, hStdoutWr = NULL;
    HANDLE hStderrRd = NULL, hStderrWr = NULL;

    if (!CreatePipe(&hStdinRd, &hStdinWr, &sa, 0)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0)) {
        CloseHandle(hStdinRd); CloseHandle(hStdinWr);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!CreatePipe(&hStderrRd, &hStderrWr, &sa, 0)) {
        CloseHandle(hStdinRd); CloseHandle(hStdinWr);
        CloseHandle(hStdoutRd); CloseHandle(hStdoutWr);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    SetHandleInformation(hStdinWr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStderrRd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi;
    bool ok = EncoderController::StartProcess(m_plan.main_command, hStdinRd, hStdoutWr, hStderrWr, &pi);
    DWORD startError = ok ? ERROR_SUCCESS : GetLastError();

    CloseHandle(hStdinRd);
    CloseHandle(hStdoutWr);
    CloseHandle(hStderrWr);

    if (!ok) {
        CloseHandle(hStdinWr);
        CloseHandle(hStdoutRd);
        CloseHandle(hStderrRd);
        return HRESULT_FROM_WIN32(startError);
    }

    m_hProc = pi.hProcess;
    m_hChildStdinW = hStdinWr;
    m_hChildStdoutR = hStdoutRd;
    m_hChildStderrR = hStderrRd;
    CloseHandle(pi.hThread);

    m_hThread = CreateThread(NULL, 0, ReaderThreadProc, this, 0, NULL);
    m_hStderrThread = CreateThread(NULL, 0, StderrThreadProc, this, 0, NULL);
    if (!m_hThread || !m_hStderrThread) {
        DWORD threadError = GetLastError();
        TerminateProcess(m_hProc, 1);
        StopFFmpeg();
        return HRESULT_FROM_WIN32(threadError);
    }
    return S_OK;
}

void CFFmpegEncoder::StopFFmpeg() {
    if (m_hChildStdinW) {
        CloseHandle(m_hChildStdinW);
        m_hChildStdinW = NULL;
    }
    if (m_hProc) {
        if (WaitForSingleObject(m_hProc, kStopWaitMs) != WAIT_OBJECT_0) {
            TerminateProcess(m_hProc, 1);
            WaitForSingleObject(m_hProc, 2000);
        }
    }
    EncoderController::JoinReaderThread(m_hThread, 5000);
    m_hThread = NULL;
    EncoderController::JoinReaderThread(m_hStderrThread, 5000);
    m_hStderrThread = NULL;
    if (m_hChildStdoutR) {
        CloseHandle(m_hChildStdoutR);
        m_hChildStdoutR = NULL;
    }
    if (m_hChildStderrR) {
        CloseHandle(m_hChildStderrR);
        m_hChildStderrR = NULL;
    }
    if (m_hProc) {
        CloseHandle(m_hProc);
        m_hProc = NULL;
    }
}

bool CFFmpegEncoder::AlphaChannelEmpty(const BYTE* data, long cb) const {
    if (m_bpp != 32 || !data || m_stride <= 0) return false;
    for (int y = 0; y < m_height; ++y) {
        long rowStart = static_cast<long>(y) * m_stride;
        if (rowStart + m_rowBytes > cb) return false;
        const BYTE* row = data + rowStart;
        for (int x = 0; x < m_width; ++x) {
            if (row[x * 4 + 3] != 0) return false;
        }
    }
    return true;
}

HRESULT CFFmpegEncoder::LaunchForFirstFrame(const BYTE* data, long cb) {
    m_launchPending = false;
    std::wstring inputPixFmt = m_pixfmt;
    if (m_config.alpha_enabled && m_pixfmt == L"bgra" && AlphaChannelEmpty(data, cb)) {
        inputPixFmt = L"bgr0";
        m_alphaIgnored = true;
    }
    m_plan = EncoderController::PrepareExecutionPlan(m_config, m_width, m_height, m_frameDur,
                                                     inputPixFmt, m_bottomUp, m_aviPath, m_mmd);
    std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", m_config.ffmpeg_path);
    m_ffmpegMissing = !winutil::FileExists(ffmpeg);
    if (m_ffmpegMissing) {
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    return StartFFmpeg();
}

std::wstring CFFmpegEncoder::LastErrorLine() const {
    std::wstring text = EncoderController::Utf8ToWide(m_stderrBuffer);
    size_t end = text.find_last_not_of(L"\r\n ");
    if (end == std::wstring::npos) return std::wstring();
    size_t start = text.find_last_of(L"\r\n", end);
    std::wstring line = text.substr(start == std::wstring::npos ? 0 : start + 1, end - (start == std::wstring::npos ? 0 : start + 1) + 1);
    if (line.size() > 200) line = line.substr(0, 200);
    return L"\n\nffmpeg: " + line;
}

void CFFmpegEncoder::NotifyProblem(const std::wstring& detail) const {
    int lang = m_config.ResolvedLanguage();
    std::wstring logDir = EncoderConfig::GetLogDirectoryPath();
    std::wstring text;
    if (lang == 2) {
        text = L"MMDirectEncoder could not finish the export correctly.\n\n" + detail + L"\n\nLog: " + logDir + L"\\latest.log";
    } else {
        text = L"MMDirectEncoder の出力で問題が発生しました。\n\n" + detail + L"\n\nログ: " + logDir + L"\\latest.log";
    }
    MessageBoxW(NULL, text.c_str(), L"MMDirectEncoder", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
}

void CFFmpegEncoder::PostProcessOutputs(bool aborted) {
    auto endTime = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(endTime - m_streamStartTime).count();
    DWORD exitCode = m_completed ? 0 : 1;
    std::wstring logDir = EncoderConfig::GetLogDirectoryPath();

    int lang = m_config.ResolvedLanguage();
    auto tr =[lang](const std::wstring& ja, const std::wstring& en) -> std::wstring {
        if (lang == 2) return en;
        return ja;
    };

    if (m_framesReceived == 0) {
        EncoderController::WriteExportLog(logDir, m_plan, m_mmd, m_width, m_height, 0, exitCode, elapsed,
                                          m_pipeFailed ? L"Failed: ffmpeg could not start" : (aborted ? L"Aborted before the first frame" : L"No frames received"), m_stderrBuffer);
        if (m_pipeFailed && m_ffmpegMissing) {
            NotifyProblem(tr(L"変換に使う ffmpeg.exe が見つかりませんでした。\n\n考えられる原因: ファイルが削除された、またはウイルス対策ソフトに隔離された。\n対処: MMDirectEncoder のインストーラーをもう一度実行してください。",
                             L"ffmpeg.exe, which is used for conversion, was not found.\n\nPossible cause: the file was deleted or quarantined by antivirus software.\nWhat to do: run the MMDirectEncoder installer again."));
        } else if (m_pipeFailed) {
            NotifyProblem(tr(L"ffmpeg.exe を起動できない、またはすぐに止まりました。\n\n考えられる原因: ウイルス対策ソフトが実行を止めた、またはファイルが壊れている。\n対処: インストーラーをもう一度実行し、それでも直らない場合はウイルス対策ソフトの除外設定に MMDirectEncoder のフォルダーを追加してください。" + LastErrorLine(),
                             L"ffmpeg.exe could not be started or stopped immediately.\n\nPossible cause: antivirus software blocked it, or the file is damaged.\nWhat to do: run the installer again. If that does not help, add the MMDirectEncoder folder to the antivirus exclusions." + LastErrorLine()));
        }
        return;
    }

    if (!m_completed) {
        EncoderController::WriteExportLog(logDir, m_plan, m_mmd, m_width, m_height, m_framesReceived, exitCode, elapsed,
                                          (aborted && !m_pipeFailed && !m_nativeFailed) ? L"Aborted" : L"Failed", m_stderrBuffer);
        if (m_nativeFailed) {
            NotifyProblem(tr(L"EXR ファイルを書き込めませんでした。\n\n考えられる原因: 保存先の空き容量不足、書き込みできない場所（保護されたフォルダーなど）。\n対処: 空き容量を確保するか、ドキュメントなど書き込みできる場所へ保存し直してください。",
                             L"The EXR files could not be written.\n\nPossible cause: not enough free space, or the destination cannot be written to (for example a protected folder).\nWhat to do: free up space or save to a writable place such as Documents."));
        } else if (!aborted || m_pipeFailed) {
            NotifyProblem(tr(L"変換が途中で止まりました。\n\n考えられる原因: 保存先の空き容量不足、GPU ドライバーの不具合、設定とビデオカードの組み合わせ。\n対処: 空き容量を確認し、それでも直らない場合は設定画面で「エンコーダー」を「CPU」にして出力し直してください。" + LastErrorLine(),
                             L"The conversion stopped partway.\n\nPossible cause: not enough free space, a GPU driver problem, or settings the video card does not support.\nWhat to do: check free space. If that does not help, set Encoder to CPU in the settings and export again." + LastErrorLine()));
        }
        return;
    }

    if (!winutil::FileExists(m_plan.primary_output_path)) {
        EncoderController::WriteExportLog(logDir, m_plan, m_mmd, m_width, m_height, m_framesReceived, exitCode, elapsed,
                                          L"Failed: output file not found", m_stderrBuffer);
        if (m_aviPath.empty()) {
            NotifyProblem(tr(L"MMD から保存先を取得できなかったため、出力できませんでした。\n対処: MMD の「AVIファイルに出力」をもう一度やり直してください。",
                             L"The destination could not be obtained from MMD, so nothing was exported.\nWhat to do: run MMD's AVI export again."));
            return;
        }
        NotifyProblem(tr(L"変換後のファイルが見つかりませんでした。\n\n考えられる原因: 保存先に書き込めない、またはウイルス対策ソフトがファイルを削除した。\n対処: ドキュメントなど書き込みできる場所へ保存し直してください。",
                         L"The converted file was not found.\n\nPossible cause: the destination cannot be written to, or antivirus software removed the file.\nWhat to do: save to a writable place such as Documents."));
        return;
    }

    std::wstring result = L"Succeeded";
    std::wstring problem;

    if (m_plan.is_image_sequence && m_plan.sequence_renumber) {
        if (!EncoderController::RenumberSequence(m_plan, m_framesReceived)) {
            result = L"Succeeded: some frames could not be renamed";
            problem = tr(L"連番の一部を改名できませんでした。",
                         L"Some frames of the sequence could not be renamed.");
        }
    }

    bool aviExists = winutil::FileExists(m_aviPath);
    bool wantAudio = m_config.audio_enabled && m_config.merge_audio && !m_plan.audio_codec.empty() &&
                     !(m_mmd.valid && !m_mmd.wave_enabled);
    bool audioOk = true;
    std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", m_config.ffmpeg_path);
    std::wstring audioSource;
    double audioOffset = 0.0;
    if (wantAudio) {
        if (aviExists && EncoderController::HasAudioStream(ffmpeg, m_aviPath)) {
            audioSource = m_aviPath;
        } else if (m_mmd.valid && !m_mmd.wav_path.empty() && m_mmd.fps > 0.0) {
            audioSource = m_mmd.wav_path;
            audioOffset = static_cast<double>(m_mmd.start_frame) / m_mmd.fps;
        }
        if (audioSource.empty()) {
            wantAudio = false;
            result = L"Succeeded: no audio source";
        }
    }
    if (wantAudio) {
        std::string mergeLog;
        double fps = (m_mmd.valid && m_mmd.fps > 0.0) ? m_mmd.fps : ((m_frameDur > 0) ? 10000000.0 / static_cast<double>(m_frameDur) : 0.0);
        double duration = (fps > 0.0) ? static_cast<double>(m_framesReceived) / fps : 0.0;
        audioOk = EncoderController::MergeAudio(ffmpeg, m_plan.primary_output_path, audioSource, audioOffset, duration, m_plan.audio_codec, &mergeLog);
        if (audioOk && audioSource != m_aviPath) {
            result = L"Succeeded: audio taken from the MMD WAV file";
        }
        if (!mergeLog.empty()) {
            m_stderrBuffer += mergeLog;
        }
        if (!audioOk) {
            result = L"Succeeded: audio merge failed";
            problem = tr(L"音声を付けられなかったため、映像のみで出力しました。",
                         L"Audio could not be added, so the file was exported without audio.");
        }
    }

    if (m_alphaIgnored) {
        result += L" (alpha channel was empty and treated as opaque)";
    }
    EncoderController::WriteExportLog(logDir, m_plan, m_mmd, m_width, m_height, m_framesReceived, exitCode, elapsed,
                                      result, m_stderrBuffer);
    if (!problem.empty()) {
        NotifyProblem(problem);
    }
}

HRESULT CFFmpegEncoder::WriteFrame(const BYTE* data, long cb) {
    if (!m_hChildStdinW || !data || cb <= 0) return E_FAIL;
    const BYTE* src = data;
    DWORD size = static_cast<DWORD>(cb);
    if (m_rowBytes > 0 && m_height > 0) {
        LONGLONG packed = static_cast<LONGLONG>(m_rowBytes) * m_height;
        LONGLONG strided = static_cast<LONGLONG>(m_stride) * (m_height - 1) + m_rowBytes;
        if (cb < strided) return E_FAIL;
        if (m_stride != m_rowBytes) {
            m_repack.resize(static_cast<size_t>(packed));
            for (int y = 0; y < m_height; ++y) {
                memcpy(m_repack.data() + static_cast<size_t>(y) * m_rowBytes,
                       data + static_cast<size_t>(y) * m_stride,
                       static_cast<size_t>(m_rowBytes));
            }
            src = m_repack.data();
        }
        size = static_cast<DWORD>(packed);
    }
    DWORD total = 0;
    while (total < size) {
        DWORD written = 0;
        if (!WriteFile(m_hChildStdinW, src + total, size - total, &written, NULL) || written == 0) {
            return E_FAIL;
        }
        total += written;
    }
    return S_OK;
}

HRESULT CFFmpegEncoder::Receive(IMediaSample* pSample) {
    try {
        return ReceiveFrame(pSample);
    } catch (...) {
        m_pipeFailed = true;
        return S_OK;
    }
}

HRESULT CFFmpegEncoder::ReceiveFrame(IMediaSample* pSample) {
    if (!pSample) return E_POINTER;
    if (m_pInput) {
        AM_SAMPLE2_PROPERTIES* pProps = m_pInput->SampleProps();
        if (pProps && pProps->dwStreamId != AM_STREAM_MEDIA) {
            return m_pOutput ? m_pOutput->Deliver(pSample) : S_OK;
        }
    }
    if (pSample->IsPreroll() == S_OK) {
        return m_pOutput ? m_pOutput->Deliver(pSample) : S_OK;
    }
    if (!m_started) return VFW_E_WRONG_STATE;

    BYTE* pData = NULL;
    HRESULT hr = pSample->GetPointer(&pData);
    if (FAILED(hr) || !pData) return hr;
    long len = pSample->GetActualDataLength();

    if (m_pipeFailed || m_nativeFailed) {
        return S_OK;
    }

    if (m_launchPending) {
        HRESULT launch = LaunchForFirstFrame(pData, len);
        if (FAILED(launch)) {
            m_pipeFailed = true;
            return S_OK;
        }
    }

    REFERENCE_TIME tStart = 0, tEnd = 0;
    if (SUCCEEDED(pSample->GetTime(&tStart, &tEnd))) {
        CAutoLock lock(&m_lock);
        m_tsQueue.push_back(tStart);
    } else {
        CAutoLock lock(&m_lock);
        m_tsQueue.push_back(m_lastTs);
    }

    HRESULT wHr = WriteFrame(pData, len);
    if (FAILED(wHr)) {
        m_pipeFailed = true;
        return S_OK;
    }

    if (m_plan.native_exr && !m_plan.primary_output_path.empty()) {
        std::wstring framePath = EncoderController::SequenceFramePath(m_plan, m_plan.sequence_start + m_framesReceived);
        bool alpha = m_config.alpha_enabled && !m_alphaIgnored && m_bpp == 32;
        if (!WriteExrFrame(framePath, pData, m_width, m_height, m_stride, m_pixfmt, m_bottomUp, alpha)) {
            m_nativeFailed = true;
            return S_OK;
        }
    }

    m_framesReceived++;
    return DrainQueue();
}

HRESULT CFFmpegEncoder::Transform(IMediaSample*, IMediaSample*) {
    return S_OK;
}

HRESULT CFFmpegEncoder::DrainQueue() {
    for (;;) {
        Packet pkt;
        {
            CAutoLock lock(&m_lock);
            if (m_pktQueue.empty()) break;
            pkt = std::move(m_pktQueue.front());
            m_pktQueue.pop_front();
        }
        HRESULT hr = DeliverPacket(pkt);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

HRESULT CFFmpegEncoder::DeliverPacket(Packet& pkt) {
    if (pkt.data.empty()) return S_OK;
    IMediaSample* pOut = NULL;
    HRESULT hr = m_pOutput->GetDeliveryBuffer(&pOut, NULL, NULL, 0);
    if (FAILED(hr) || !pOut) return hr;

    long cap = pOut->GetSize();
    if (static_cast<long>(pkt.data.size()) > cap) {
        pOut->Release();
        return E_OUTOFMEMORY;
    }

    BYTE* dst = NULL;
    if (FAILED(pOut->GetPointer(&dst)) || !dst) {
        pOut->Release();
        return E_UNEXPECTED;
    }
    memcpy(dst, pkt.data.data(), pkt.data.size());
    pOut->SetActualDataLength(static_cast<long>(pkt.data.size()));

    REFERENCE_TIME ts = 0;
    {
        CAutoLock lock(&m_lock);
        if (!m_tsQueue.empty()) {
            ts = m_tsQueue.front();
            m_tsQueue.pop_front();
            m_lastTs = ts;
        } else {
            ts = m_lastTs + m_frameDur;
            m_lastTs = ts;
        }
    }
    REFERENCE_TIME tEnd = ts + m_frameDur;
    pOut->SetTime(&ts, &tEnd);
    pOut->SetSyncPoint(m_firstPkt ? TRUE : FALSE);
    m_firstPkt = false;

    hr = m_pOutput->Deliver(pOut);
    pOut->Release();
    return hr;
}

HRESULT CFFmpegEncoder::EndOfStream() {
    m_eosReceived = true;
    if (m_hChildStdinW) {
        CloseHandle(m_hChildStdinW);
        m_hChildStdinW = NULL;
    }
    HRESULT drain = S_OK;
    DWORD exitCode = STILL_ACTIVE;
    if (m_hProc) {
        ULONGLONG deadline = GetTickCount64() + kFinishWaitMs;
        while (!m_readerDone.load() && GetTickCount64() < deadline) {
            DrainQueue();
            Sleep(5);
        }
        drain = DrainQueue();
        ULONGLONG now = GetTickCount64();
        DWORD remaining = (now < deadline) ? static_cast<DWORD>(deadline - now) : 0;
        if (WaitForSingleObject(m_hProc, remaining) == WAIT_OBJECT_0) {
            GetExitCodeProcess(m_hProc, &exitCode);
        }
    }
    m_completed = m_hProc != NULL && SUCCEEDED(drain) && exitCode == 0 && !m_nativeFailed;
    HRESULT eos = CTransformFilter::EndOfStream();
    return FAILED(drain) ? drain : eos;
}

DWORD WINAPI CFFmpegEncoder::ReaderThreadProc(LPVOID param) {
    CFFmpegEncoder* pThis = reinterpret_cast<CFFmpegEncoder*>(param);
    pThis->ReaderLoop();
    return 0;
}

void CFFmpegEncoder::ReaderLoop() {
    try {
        std::vector<BYTE> buf(65536);
        for (;;) {
            DWORD rd = 0;
            if (!ReadFile(m_hChildStdoutR, buf.data(), static_cast<DWORD>(buf.size()), &rd, NULL) || rd == 0) {
                break;
            }
            OnRead(buf.data(), rd);
        }
        CAutoLock lock(&m_lock);
        FlushGroup(true);
    } catch (...) {
        m_pipeFailed = true;
    }
    m_readerDone.store(true);
}

void CFFmpegEncoder::OnRead(const BYTE* data, size_t len) {
    CAutoLock lock(&m_lock);
    m_inBuf.insert(m_inBuf.end(), data, data + len);
    ParseAnnexB();
}

bool CFFmpegEncoder::NalIsVcl(const BYTE* nal, size_t len) const {
    if (len == 0) return false;
    BYTE h = nal[0];
    if ((h & 0x80) != 0) return false;
    BYTE type = h & 0x1F;
    return type >= 1 && type <= 5;
}

void CFFmpegEncoder::ParseAnnexB() {
    auto findStartCode = [&](size_t from, size_t* codeLen) -> size_t {
        for (size_t i = from; i + 2 < m_inBuf.size(); i++) {
            if (m_inBuf[i] == 0 && m_inBuf[i + 1] == 0) {
                if (m_inBuf[i + 2] == 1) {
                    if (i > 0 && m_inBuf[i - 1] == 0) {
                        *codeLen = 4;
                        return i - 1;
                    }
                    *codeLen = 3;
                    return i;
                }
                if (i + 3 < m_inBuf.size() && m_inBuf[i + 2] == 0 && m_inBuf[i + 3] == 1) {
                    *codeLen = 4;
                    return i;
                }
            }
        }
        return std::string::npos;
    };

    size_t codeLen = 0;
    size_t first = findStartCode(0, &codeLen);
    if (first == std::string::npos) {
        if (m_inBuf.size() > 1024 * 1024) {
            m_inBuf.clear();
        }
        return;
    }
    size_t nalStart = first + codeLen;
    size_t keepFrom = first;

    for (;;) {
        size_t nextCodeLen = 0;
        size_t next = findStartCode(nalStart, &nextCodeLen);
        if (next == std::string::npos) break;
        if (next > nalStart) {
            const BYTE* nal = m_inBuf.data() + nalStart;
            size_t nalLen = next - nalStart;
            while (nalLen > 0 && nal[nalLen - 1] == 0) {
                nalLen--;
            }
            if (nalLen > 0) {
                PushNal(nal, nalLen);
            }
        }
        keepFrom = next;
        codeLen = nextCodeLen;
        nalStart = next + codeLen;
    }

    m_inBuf.erase(m_inBuf.begin(), m_inBuf.begin() + keepFrom);
}

void CFFmpegEncoder::PushNal(const BYTE* nal, size_t len) {
    if (!nal || len == 0) return;
    bool vcl = NalIsVcl(nal, len);
    if (!vcl && m_groupHasVcl) {
        FlushGroup(false);
    }
    static const BYTE startCode[] = {0, 0, 0, 1};
    m_group.insert(m_group.end(), startCode, startCode + 4);
    m_group.insert(m_group.end(), nal, nal + len);
    if (vcl) {
        m_groupHasVcl = true;
        FlushGroup(false);
    }
}

void CFFmpegEncoder::FlushGroup(bool atEof) {
    if (atEof && !m_inBuf.empty()) {
        size_t start = std::string::npos;
        size_t codeLen = 0;
        for (size_t i = 0; i + 2 < m_inBuf.size(); ++i) {
            if (m_inBuf[i] == 0 && m_inBuf[i + 1] == 0 && m_inBuf[i + 2] == 1) {
                start = i;
                codeLen = 3;
                if (i > 0 && m_inBuf[i - 1] == 0) {
                    start = i - 1;
                    codeLen = 4;
                }
                break;
            }
            if (i + 3 < m_inBuf.size() && m_inBuf[i] == 0 && m_inBuf[i + 1] == 0 && m_inBuf[i + 2] == 0 && m_inBuf[i + 3] == 1) {
                start = i;
                codeLen = 4;
                break;
            }
        }
        if (start != std::string::npos && start + codeLen < m_inBuf.size()) {
            size_t end = m_inBuf.size();
            while (end > start + codeLen && m_inBuf[end - 1] == 0) {
                --end;
            }
            if (end > start + codeLen) {
                std::vector<BYTE> tail(m_inBuf.begin() + start + codeLen, m_inBuf.begin() + end);
                m_inBuf.clear();
                PushNal(tail.data(), tail.size());
            }
        }
        m_inBuf.clear();
    }
    if (!m_group.empty()) {
        Packet pkt;
        pkt.data.swap(m_group);
        m_group.clear();
        m_groupHasVcl = false;
        m_pktQueue.push_back(std::move(pkt));
    }
}
