#include "ffmpeg_encoder.h"
#include "../../ui/property_dialog.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <utility>

extern HINSTANCE g_hInst;

namespace {
    const GUID kMmdRgb32 = {
        0x773C9AC0, 0x3274, 0x11D0,
        {0xB7, 0x24, 0x00, 0xAA, 0x00, 0x6C, 0x1A, 0x01}};

    bool DirectFileExists(const std::wstring& path) {
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool CheckSequenceExists(const std::wstring& primaryPath) {
        if (DirectFileExists(primaryPath)) return true;
        size_t underscore = primaryPath.find_last_of(L'_');
        size_t dot = primaryPath.find_last_of(L'.');
        if (underscore != std::wstring::npos && dot != std::wstring::npos && underscore < dot) {
            std::wstring pattern = primaryPath.substr(0, underscore + 1) + L"*" + primaryPath.substr(dot);
            WIN32_FIND_DATAW wfd;
            HANDLE hFind = FindFirstFileW(pattern.c_str(), &wfd);
            if (hFind != INVALID_HANDLE_VALUE) {
                FindClose(hFind);
                return true;
            }
        }
        return false;
    }

    bool SafeDeleteFileWithRetry(const std::wstring& path, int maxRetries = 25, DWORD delayMs = 100) {
        if (path.empty()) return true;
        for (int i = 0; i < maxRetries; i++) {
            if (!DirectFileExists(path)) return true;
            if (DeleteFileW(path.c_str())) return true;
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND) return true;
            Sleep(delayMs);
        }
        return !DirectFileExists(path);
    }
}

DWORD WINAPI CFFmpegEncoder::StderrThreadProc(LPVOID param) {
    CFFmpegEncoder* pThis = reinterpret_cast<CFFmpegEncoder*>(param);
    char buf[4096];
    DWORD rd = 0;
    while (ReadFile(pThis->m_hChildStderrR, buf, sizeof(buf), &rd, NULL) && rd > 0) {
        pThis->m_stderrBuffer.append(buf, rd);
    }
    return 0;
}

CUnknown* WINAPI CFFmpegEncoder::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
    return new CFFmpegEncoder(pUnk, phr);
}

CFFmpegEncoder::CFFmpegEncoder(LPUNKNOWN pUnk, HRESULT* phr)
    : CTransformFilter(NAME("MMDirect Encoder"), pUnk, CLSID_FFmpegEncoder)
{
    m_config.Load();
    if (phr) {
        *phr = S_OK;
    }
}

CFFmpegEncoder::~CFFmpegEncoder() {
    StopFFmpeg();
    if (m_config.delete_avi && !m_aviPath.empty()) {
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
        MessageBoxW(hwnd, L"MMDirect Encoder\nH.264 / HEVC / AV1 DirectShow Filter", L"MMDirect Encoder", MB_OK | MB_ICONINFORMATION);
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
          IsEqualGUID(*mtIn->Subtype(), MEDIASUBTYPE_RGB8) ||
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

    DWORD expectedFourcc = (m_config.format == L"hevc") ?
        ((static_cast<DWORD>('H')) | (static_cast<DWORD>('E') << 8) | (static_cast<DWORD>('V') << 16) | (static_cast<DWORD>('C') << 24)) :
        ((static_cast<DWORD>('H')) | (static_cast<DWORD>('2') << 8) | (static_cast<DWORD>('6') << 16) | (static_cast<DWORD>('4') << 24));

    GUID expected = FourccGuid(expectedFourcc);
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

    DWORD fcc = (m_config.format == L"hevc") ?
        ((static_cast<DWORD>('H')) | (static_cast<DWORD>('E') << 8) | (static_cast<DWORD>('V') << 16) | (static_cast<DWORD>('C') << 24)) :
        ((static_cast<DWORD>('H')) | (static_cast<DWORD>('2') << 8) | (static_cast<DWORD>('6') << 16) | (static_cast<DWORD>('4') << 24));

    m_fourcc = fcc;

    VIDEOINFOHEADER vih;
    ZeroMemory(&vih, sizeof(vih));
    vih.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih.bmiHeader.biWidth = m_width;
    vih.bmiHeader.biHeight = m_height;
    vih.bmiHeader.biPlanes = 1;
    vih.bmiHeader.biBitCount = 24;
    vih.bmiHeader.biCompression = m_fourcc;
    vih.bmiHeader.biSizeImage = 0;
    vih.AvgTimePerFrame = m_frameDur;
    SetRect(&vih.rcSource, 0, 0, m_width, m_height);
    vih.rcTarget = vih.rcSource;

    GUID sub = FourccGuid(m_fourcc);
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
        } else if (IsEqualGUID(m_inSubtype, MEDIASUBTYPE_RGB565) ||
                   IsEqualGUID(m_inSubtype, MEDIASUBTYPE_RGB555)) {
            m_pixfmt = L"rgb565le"; m_bpp = 16;
        } else if (IsEqualGUID(m_inSubtype, MEDIASUBTYPE_RGB8)) {
            m_pixfmt = L"gray"; m_bpp = 8;
        } else {
            return VFW_E_INVALIDMEDIATYPE;
        }
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
    m_framesReceived = 0;
    m_stderrBuffer.clear();
    m_streamStartTime = std::chrono::steady_clock::now();

    m_config.Load();
    ResolveOutputPaths();

    double fps = (m_frameDur > 0) ? (10000000.0 / static_cast<double>(m_frameDur)) : 30.0;
    m_plan = EncoderController::PrepareExecutionPlan(m_config, m_width, m_height, fps, m_pixfmt, m_bottomUp, m_aviPath);
    m_outMux = m_plan.elementary_muxer;
    m_fourcc = m_plan.elementary_fourcc;

    HRESULT hr = StartFFmpeg();
    m_started = SUCCEEDED(hr);
    return hr;
}

HRESULT CFFmpegEncoder::StopStreaming() {
    StopFFmpeg();
    PostProcessOutputs();
    m_started = false;
    return S_OK;
}

HRESULT CFFmpegEncoder::StartFFmpeg() {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

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

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRd;
    si.hStdOutput = hStdoutWr;
    si.hStdError = hStderrWr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmd = m_plan.main_command;
    BOOL ok = CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    CloseHandle(hStdinRd);
    CloseHandle(hStdoutWr);
    CloseHandle(hStderrWr);

    if (!ok) {
        CloseHandle(hStdinWr);
        CloseHandle(hStdoutRd);
        CloseHandle(hStderrRd);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    m_hProc = pi.hProcess;
    m_hChildStdinW = hStdinWr;
    m_hChildStdoutR = hStdoutRd;
    m_hChildStderrR = hStderrRd;
    CloseHandle(pi.hThread);

    m_hThread = CreateThread(NULL, 0, ReaderThreadProc, this, 0, NULL);
    m_hStderrThread = CreateThread(NULL, 0, StderrThreadProc, this, 0, NULL);

    return S_OK;
}

void CFFmpegEncoder::StopFFmpeg() {
    if (m_hChildStdinW) {
        CloseHandle(m_hChildStdinW);
        m_hChildStdinW = NULL;
    }
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 5000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
    if (m_hStderrThread) {
        WaitForSingleObject(m_hStderrThread, 2000);
        CloseHandle(m_hStderrThread);
        m_hStderrThread = NULL;
    }
    if (m_hChildStdoutR) {
        CloseHandle(m_hChildStdoutR);
        m_hChildStdoutR = NULL;
    }
    if (m_hChildStderrR) {
        CloseHandle(m_hChildStderrR);
        m_hChildStderrR = NULL;
    }
    if (m_hProc) {
        if (WaitForSingleObject(m_hProc, 5000) != WAIT_OBJECT_0) {
            TerminateProcess(m_hProc, 1);
            WaitForSingleObject(m_hProc, 1000);
        }
        CloseHandle(m_hProc);
        m_hProc = NULL;
    }
}

void CFFmpegEncoder::PostProcessOutputs() {
    auto endTime = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(endTime - m_streamStartTime).count();
    DWORD exitCode = m_completed ? 0 : 1;
    double fps = (m_frameDur > 0) ? (10000000.0 / static_cast<double>(m_frameDur)) : 30.0;

    std::wstring logDir = EncoderConfig::GetLogDirectoryPath();
    EncoderController::WriteExportLog(logDir, m_plan, m_width, m_height, fps,
                                      m_framesReceived, exitCode, elapsed, m_stderrBuffer);

    if (m_completed) {
        bool outputExists = false;
        if (m_plan.is_image_sequence) {
            outputExists = CheckSequenceExists(m_plan.primary_output_path);
        } else if (!m_plan.primary_output_path.empty() && DirectFileExists(m_plan.primary_output_path)) {
            outputExists = true;
        }

        if (outputExists) {
            if (m_config.audio_enabled && m_config.merge_audio && !m_aviPath.empty() && DirectFileExists(m_aviPath)) {
                std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", m_config.ffmpeg_path);
                if (m_plan.is_image_sequence) {
                    if (!m_plan.audio_output_path.empty()) {
                        EncoderController::ExtractAudio(ffmpeg, m_aviPath, m_plan.audio_output_path, nullptr);
                    }
                    if (m_config.delete_avi) {
                        SafeDeleteFileWithRetry(m_aviPath);
                    }
                } else {
                    bool isWebM = m_plan.primary_output_path.find(L".webm") != std::wstring::npos;
                    EncoderController::MergeAudio(ffmpeg, m_plan.primary_output_path, m_aviPath, isWebM, nullptr);
                    if (m_config.delete_avi) {
                        SafeDeleteFileWithRetry(m_aviPath);
                    }
                }
            } else if (m_config.delete_avi && !m_aviPath.empty()) {
                SafeDeleteFileWithRetry(m_aviPath);
            }
        }
    }
}

HRESULT CFFmpegEncoder::WriteFrame(const BYTE* data, long cb) {
    if (!m_hChildStdinW || !data || cb <= 0) return E_FAIL;
    DWORD total = 0;
    while (total < static_cast<DWORD>(cb)) {
        DWORD written = 0;
        if (!WriteFile(m_hChildStdinW, data + total, static_cast<DWORD>(cb) - total, &written, NULL) || written == 0) {
            return E_FAIL;
        }
        total += written;
    }
    return S_OK;
}

HRESULT CFFmpegEncoder::Receive(IMediaSample* pSample) {
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

    REFERENCE_TIME tStart = 0, tEnd = 0;
    if (SUCCEEDED(pSample->GetTime(&tStart, &tEnd))) {
        CAutoLock lock(&m_lock);
        m_tsQueue.push_back(tStart);
    } else {
        CAutoLock lock(&m_lock);
        m_tsQueue.push_back(m_lastTs);
    }

    BYTE* pData = NULL;
    HRESULT hr = pSample->GetPointer(&pData);
    if (FAILED(hr) || !pData) return hr;
    long len = pSample->GetActualDataLength();

    HRESULT wHr = WriteFrame(pData, len);
    if (FAILED(wHr)) return wHr;

    m_framesReceived++;
    return DrainQueue();
}

HRESULT CFFmpegEncoder::Transform(IMediaSample* pIn, IMediaSample* pOut) {
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
    pOut->GetPointer(&dst);
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
    if (m_hChildStdinW) {
        CloseHandle(m_hChildStdinW);
        m_hChildStdinW = NULL;
    }
    while (!m_readerDone.load()) {
        DrainQueue();
        Sleep(5);
    }
    HRESULT drain = DrainQueue();
    DWORD exitCode = STILL_ACTIVE;
    if (m_hProc && WaitForSingleObject(m_hProc, 5000) == WAIT_OBJECT_0) {
        GetExitCodeProcess(m_hProc, &exitCode);
    }
    m_completed = SUCCEEDED(drain) && exitCode == 0;
    HRESULT eos = CTransformFilter::EndOfStream();
    return FAILED(drain) ? drain : eos;
}

DWORD WINAPI CFFmpegEncoder::ReaderThreadProc(LPVOID param) {
    CFFmpegEncoder* pThis = reinterpret_cast<CFFmpegEncoder*>(param);
    pThis->ReaderLoop();
    return 0;
}

void CFFmpegEncoder::ReaderLoop() {
    BYTE buf[65536];
    for (;;) {
        DWORD rd = 0;
        if (!ReadFile(m_hChildStdoutR, buf, sizeof(buf), &rd, NULL) || rd == 0) {
            break;
        }
        OnRead(buf, rd);
    }
    {
        CAutoLock lock(&m_lock);
        FlushGroup(true);
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
    if (m_outMux == L"h264") {
        BYTE type = h & 0x1F;
        return type >= 1 && type <= 5;
    }
    if (m_outMux == L"hevc") {
        return ((h >> 1) & 0x3F) <= 31;
    }
    return true;
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
