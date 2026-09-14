#include "mrecord.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#define AUDIO_BUFFER_HNS   2000000
#define AUDIO_POLL_MS      10
#define AUDIO_LAG_HNS      1000000
#define SILENCE_CHUNK      4800

static const GUID MR_CLSID_MMDeviceEnumerator =
    { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const GUID MR_IID_IMMDeviceEnumerator =
    { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static const GUID MR_IID_IAudioClient =
    { 0x1cb9ad4c, 0xdbfa, 0x4c32, { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
static const GUID MR_IID_IAudioCaptureClient =
    { 0xc8adbd64, 0xe71e, 0x48a0, { 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17 } };

static void describe_format(Audio *a, const WAVEFORMATEX *fmt) {
    a->channels = fmt->nChannels;
    a->bits     = fmt->wBitsPerSample;
    a->rate_in  = (int)fmt->nSamplesPerSec;
    a->is_float = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)fmt;
        a->is_float = ext->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
    }
}

bool audio_open(Audio *a) {
    ZeroMemory(a, sizeof *a);

    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice           *device     = NULL;
    WAVEFORMATEX        *fmt        = NULL;

    HRESULT hr = CoCreateInstance(&MR_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                  &MR_IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (SUCCEEDED(hr)) hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    if (SUCCEEDED(hr)) hr = IMMDevice_Activate(device, &MR_IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&a->client);
    if (SUCCEEDED(hr)) hr = IAudioClient_GetMixFormat(a->client, &fmt);
    if (SUCCEEDED(hr)) {
        describe_format(a, fmt);
        int factor = convert_decimation_factor(a->rate_in);
        if (factor == 0 || a->channels < 1 || (a->bits != 8 && a->bits != 16 &&
                                               a->bits != 24 && a->bits != 32 &&
                                               a->bits != 64)) {
            log_msg(LOG_ERROR, L"audio: unsupported mix format %d Hz, %d channels, %d bits",
                    a->rate_in, a->channels, a->bits);
            hr = E_FAIL;
        } else {
            a->rate_out = a->rate_in / factor;
            decimator_init(&a->decimator, factor);
        }
    }
    if (SUCCEEDED(hr))
        hr = IAudioClient_Initialize(a->client, AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_LOOPBACK, AUDIO_BUFFER_HNS,
                                     0, fmt, NULL);
    if (SUCCEEDED(hr))
        hr = IAudioClient_GetService(a->client, &MR_IID_IAudioCaptureClient, (void **)&a->capture);

    if (fmt) CoTaskMemFree(fmt);
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);

    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"audio: cannot open system sound loopback (0x%08lX)", (unsigned long)hr);
        audio_close(a);
        return false;
    }

    log_msg(LOG_INFO, L"audio: loopback %d Hz, %d channels, %d-bit %ls -> %d Hz stereo",
            a->rate_in, a->channels, a->bits, a->is_float ? L"float" : L"int", a->rate_out);
    return true;
}

static int64_t now_ticks(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static bool write_silence(Audio *a, int64_t frames) {
    static const int16_t zeros[SILENCE_CHUNK * 2];
    while (frames > 0) {
        uint32_t n = frames > SILENCE_CHUNK ? SILENCE_CHUNK : (uint32_t)frames;
        int64_t  t = pacing_audio_time(a->frames_written, a->rate_out);
        int64_t  d = pacing_audio_time(a->frames_written + n, a->rate_out) - t;
        if (!encoder_write_audio(a->encoder, zeros, n, t, d)) return false;
        a->frames_written += n;
        frames -= n;
    }
    return true;
}

static bool write_pcm(Audio *a, const int16_t *pcm, uint32_t frames) {
    int64_t elapsed = pacing_ticks_to_hns(now_ticks() - a->t0_ticks, a->ticks_per_second);
    int64_t packet  = pacing_audio_time(frames, a->rate_out);
    int64_t gap     = pacing_silence_frames(a->frames_written, elapsed - packet, 0, a->rate_out);
    if (gap > 0 && !write_silence(a, gap)) return false;

    int64_t t = pacing_audio_time(a->frames_written, a->rate_out);
    int64_t d = pacing_audio_time(a->frames_written + frames, a->rate_out) - t;
    if (!encoder_write_audio(a->encoder, pcm, frames, t, d)) return false;
    a->frames_written += frames;
    return true;
}

static bool ensure_buffer(Audio *a, size_t frames) {
    if (a->buffer_frames >= frames) return true;
    int16_t *grown = (int16_t *)realloc(a->buffer, frames * 2 * sizeof(int16_t));
    if (!grown) return false;
    a->buffer        = grown;
    a->buffer_frames = frames;
    return true;
}

static bool drain(Audio *a) {
    for (;;) {
        UINT32 packet = 0;
        HRESULT hr = IAudioCaptureClient_GetNextPacketSize(a->capture, &packet);
        if (FAILED(hr)) {
            log_msg(LOG_ERROR, L"audio: GetNextPacketSize failed (0x%08lX)", (unsigned long)hr);
            return false;
        }
        if (packet == 0) return true;

        BYTE  *data   = NULL;
        UINT32 frames = 0;
        DWORD  flags  = 0;
        hr = IAudioCaptureClient_GetBuffer(a->capture, &data, &frames, &flags, NULL, NULL);
        if (FAILED(hr)) {
            log_msg(LOG_ERROR, L"audio: GetBuffer failed (0x%08lX)", (unsigned long)hr);
            return false;
        }

        bool ok = ensure_buffer(a, frames);
        if (ok && frames) {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                memset(a->buffer, 0, (size_t)frames * 2 * sizeof(int16_t));
            else
                convert_to_s16_stereo(data, frames, a->channels, a->bits, a->is_float, a->buffer);
            size_t out = decimator_run(&a->decimator, a->buffer, frames, a->buffer);
            ok = write_pcm(a, a->buffer, (uint32_t)out);
        }

        IAudioCaptureClient_ReleaseBuffer(a->capture, frames);
        if (!ok) return false;
    }
}

static DWORD WINAPI audio_thread(LPVOID param) {
    Audio *a = (Audio *)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    while (!InterlockedCompareExchange(&a->stop, 0, 0)) {
        Sleep(AUDIO_POLL_MS);
        if (!drain(a)) {
            InterlockedExchange(&a->failed, 1);
            break;
        }
        int64_t elapsed = pacing_ticks_to_hns(now_ticks() - a->t0_ticks, a->ticks_per_second);
        int64_t gap = pacing_silence_frames(a->frames_written, elapsed, AUDIO_LAG_HNS, a->rate_out);
        if (gap > 0 && !write_silence(a, gap)) {
            InterlockedExchange(&a->failed, 1);
            break;
        }
    }

    CoUninitialize();
    return 0;
}

bool audio_start(Audio *a, Encoder *encoder, int64_t t0_ticks, int64_t ticks_per_second) {
    a->encoder          = encoder;
    a->t0_ticks         = t0_ticks;
    a->ticks_per_second = ticks_per_second;
    a->frames_written   = 0;

    HRESULT hr = IAudioClient_Start(a->client);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"audio: Start failed (0x%08lX)", (unsigned long)hr);
        return false;
    }

    a->thread = CreateThread(NULL, 0, audio_thread, a, 0, NULL);
    if (!a->thread) {
        IAudioClient_Stop(a->client);
        return false;
    }
    return true;
}

void audio_stop(Audio *a) {
    if (!a->thread) return;
    InterlockedExchange(&a->stop, 1);
    WaitForSingleObject(a->thread, 5000);
    CloseHandle(a->thread);
    a->thread = NULL;
    IAudioClient_Stop(a->client);
}

void audio_close(Audio *a) {
    if (a->capture) IAudioCaptureClient_Release(a->capture);
    if (a->client)  IAudioClient_Release(a->client);
    free(a->buffer);
    a->capture       = NULL;
    a->client        = NULL;
    a->buffer        = NULL;
    a->buffer_frames = 0;
}
