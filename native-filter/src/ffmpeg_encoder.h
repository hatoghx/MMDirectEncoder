#pragma once

#include <windows.h>
#include <streams.h>
#include <dvdmedia.h>
#include <vfw.h>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <chrono>

#include "../../config/encoder_config.h"
#include "../../encoder/encoder_controller.h"

DEFINE_GUID(CLSID_FFmpegEncoder,
    0xD79D43B2, 0xF005, 0x40A4, 0xBE, 0x18, 0xAF, 0xD1, 0x9C, 0x03, 0xE6, 0xE6);

class CFFmpegEncoder : public CTransformFilter,
                       public IAMVfwCompressDialogs,
                       public ISpecifyPropertyPages
{
public:
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr);

    CFFmpegEncoder(LPUNKNOWN pUnk, HRESULT* phr);
    ~CFFmpegEncoder();

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override;

    HRESULT CheckInputType(const CMediaType* mtIn) override;
    HRESULT CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut) override;
    HRESULT GetMediaType(int iPosition, CMediaType* pmt) override;
    HRESULT SetMediaType(PIN_DIRECTION direction, const CMediaType* pmt) override;
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) override;
    HRESULT StartStreaming() override;
    HRESULT StopStreaming() override;
    HRESULT Transform(IMediaSample* pIn, IMediaSample* pOut) override;
    HRESULT Receive(IMediaSample* pSample) override;
    HRESULT EndOfStream() override;

    STDMETHODIMP ShowDialog(int iDialog, HWND hwnd) override;
    STDMETHODIMP GetState(LPVOID, int*) override;
    STDMETHODIMP SetState(LPVOID, int) override;
    STDMETHODIMP SendDriverMessage(int, long, long) override;

    STDMETHODIMP GetPages(CAUUID* pPages) override;

private:
    struct Packet {
        std::vector<BYTE> data;
    };

    HRESULT StartFFmpeg();
    void    StopFFmpeg();
    void    ResolveOutputPaths();
    HRESULT LaunchForFirstFrame(const BYTE* data, long cb);
    bool    AlphaChannelEmpty(const BYTE* data, long cb) const;
    void    PostProcessOutputs(bool aborted);
    void    NotifyProblem(const std::wstring& detail) const;
    std::wstring LastErrorLine() const;
    HRESULT ReceiveFrame(IMediaSample* pSample);
    HRESULT WriteFrame(const BYTE* data, long cb);
    HRESULT DrainQueue();
    HRESULT DeliverPacket(Packet& pkt);

    static DWORD WINAPI ReaderThreadProc(LPVOID param);
    static DWORD WINAPI StderrThreadProc(LPVOID param);
    void    ReaderLoop();
    void    OnRead(const BYTE* data, size_t len);
    void    ParseAnnexB();
    void    PushNal(const BYTE* nal, size_t len);
    void    FlushGroup(bool atEof);
    bool    NalIsVcl(const BYTE* nal, size_t len) const;
    static GUID FourccGuid(DWORD fcc);

    EncoderConfig m_config;
    ExecutionPlan m_plan;

    int     m_width = 0;
    int     m_height = 0;
    int     m_bpp = 0;
    bool    m_bottomUp = false;
    REFERENCE_TIME m_frameDur = 1;
    GUID    m_inSubtype = GUID_NULL;
    std::wstring m_pixfmt;
    long    m_stride = 0;
    long    m_rowBytes = 0;
    std::vector<BYTE> m_repack;

    std::wstring m_aviPath;
    MmdOutputInfo m_mmd;
    bool    m_completed = false;
    bool    m_launchPending = false;
    bool    m_eosReceived = false;
    bool    m_alphaIgnored = false;
    bool    m_nativeFailed = false;
    std::atomic<bool> m_pipeFailed{false};
    bool    m_ffmpegMissing = false;
    bool    m_aviDeletePending = false;

    long long m_framesReceived = 0;
    std::chrono::time_point<std::chrono::steady_clock> m_streamStartTime;
    std::string m_stderrBuffer;
    CCritSec m_stderrLock;

    HANDLE  m_hProc = NULL;
    HANDLE  m_hChildStdinW = NULL;
    HANDLE  m_hChildStdoutR = NULL;
    HANDLE  m_hChildStderrR = NULL;
    HANDLE  m_hThread = NULL;
    HANDLE  m_hStderrThread = NULL;
    std::atomic<bool> m_readerDone{false};

    CCritSec m_lock;
    std::vector<BYTE> m_inBuf;
    std::vector<BYTE> m_group;
    bool    m_groupHasVcl = false;
    std::deque<Packet> m_pktQueue;
    std::deque<REFERENCE_TIME> m_tsQueue;
    REFERENCE_TIME m_lastTs = 0;
    bool    m_firstPkt = true;
    bool    m_started = false;
};
