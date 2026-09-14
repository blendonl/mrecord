#include "mrecord.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>

#define AAC_BYTES_PER_SECOND 24000

static HRESULT set_pair(IMFMediaType *t, REFGUID key, UINT32 hi, UINT32 lo) {
    return IMFMediaType_SetUINT64(t, key, ((UINT64)hi << 32) | lo);
}

static HRESULT video_type(IMFMediaType **out, const GUID *subtype, int w, int h, int fps) {
    IMFMediaType *t = NULL;
    HRESULT hr = MFCreateMediaType(&t);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetGUID(t, &MF_MT_SUBTYPE, subtype);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(t, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = set_pair(t, &MF_MT_FRAME_SIZE, (UINT32)w, (UINT32)h);
    if (SUCCEEDED(hr)) hr = set_pair(t, &MF_MT_FRAME_RATE, (UINT32)fps, 1);
    if (SUCCEEDED(hr)) hr = set_pair(t, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(hr)) {
        if (t) IMFMediaType_Release(t);
        return hr;
    }
    *out = t;
    return S_OK;
}

static HRESULT add_video(Encoder *e, int fps, int kbps) {
    IMFMediaType *out = NULL, *in = NULL;

    HRESULT hr = video_type(&out, &MFVideoFormat_H264, e->width, e->height, fps);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(out, &MF_MT_AVG_BITRATE, (UINT32)kbps * 1000);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(out, &MF_MT_MPEG2_PROFILE, 100);
    if (SUCCEEDED(hr)) hr = IMFSinkWriter_AddStream(e->writer, out, &e->video_stream);
    if (out) IMFMediaType_Release(out);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"encoder: H.264 stream refused (0x%08lX)", (unsigned long)hr);
        return hr;
    }

    hr = video_type(&in, &MFVideoFormat_RGB32, e->width, e->height, fps);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(in, &MF_MT_DEFAULT_STRIDE, (UINT32)(e->width * 4));
    if (SUCCEEDED(hr)) hr = IMFSinkWriter_SetInputMediaType(e->writer, e->video_stream, in, NULL);
    if (in) IMFMediaType_Release(in);
    if (SUCCEEDED(hr)) return hr;

    log_msg(LOG_WARN, L"encoder: RGB32 input refused (0x%08lX), converting to NV12 instead",
            (unsigned long)hr);

    in = NULL;
    hr = video_type(&in, &MFVideoFormat_NV12, e->width, e->height, fps);
    if (SUCCEEDED(hr)) hr = IMFSinkWriter_SetInputMediaType(e->writer, e->video_stream, in, NULL);
    if (in) IMFMediaType_Release(in);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"encoder: NV12 input refused too (0x%08lX)", (unsigned long)hr);
        return hr;
    }
    e->nv12 = true;
    return S_OK;
}

static HRESULT audio_type(IMFMediaType **out, const GUID *subtype, int rate) {
    IMFMediaType *t = NULL;
    HRESULT hr = MFCreateMediaType(&t);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetGUID(t, &MF_MT_SUBTYPE, subtype);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_NUM_CHANNELS, 2);
    if (FAILED(hr)) {
        if (t) IMFMediaType_Release(t);
        return hr;
    }
    *out = t;
    return S_OK;
}

static HRESULT add_audio(Encoder *e) {
    IMFMediaType *out = NULL, *in = NULL;

    HRESULT hr = audio_type(&out, &MFAudioFormat_AAC, e->audio_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(out, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AAC_BYTES_PER_SECOND);
    if (SUCCEEDED(hr)) hr = IMFSinkWriter_AddStream(e->writer, out, &e->audio_stream);
    if (out) IMFMediaType_Release(out);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"encoder: AAC stream refused (0x%08lX)", (unsigned long)hr);
        return hr;
    }

    hr = audio_type(&in, &MFAudioFormat_PCM, e->audio_rate);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(in, &MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(in, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)e->audio_rate * 4);
    if (SUCCEEDED(hr)) hr = IMFMediaType_SetUINT32(in, &MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (SUCCEEDED(hr)) hr = IMFSinkWriter_SetInputMediaType(e->writer, e->audio_stream, in, NULL);
    if (in) IMFMediaType_Release(in);
    if (FAILED(hr))
        log_msg(LOG_ERROR, L"encoder: PCM input for AAC refused (0x%08lX)", (unsigned long)hr);
    return hr;
}

bool encoder_open(Encoder *e, const wchar_t *path, int width, int height,
                  int fps, int kbps, int audio_rate) {
    ZeroMemory(e, sizeof *e);
    InitializeCriticalSection(&e->lock);
    e->width      = width;
    e->height     = height;
    e->audio_rate = audio_rate;
    e->has_audio  = audio_rate > 0;

    IMFAttributes *attr = NULL;
    HRESULT hr = MFCreateAttributes(&attr, 2);
    if (SUCCEEDED(hr)) hr = IMFAttributes_SetUINT32(attr, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (SUCCEEDED(hr)) hr = IMFAttributes_SetGUID(attr, &MF_TRANSCODE_CONTAINERTYPE, &MFTranscodeContainerType_MPEG4);
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(path, NULL, attr, &e->writer);
    if (attr) IMFAttributes_Release(attr);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"encoder: cannot write %ls (0x%08lX)", path, (unsigned long)hr);
        DeleteCriticalSection(&e->lock);
        return false;
    }

    hr = add_video(e, fps, kbps);
    if (SUCCEEDED(hr) && e->has_audio) hr = add_audio(e);
    if (SUCCEEDED(hr)) hr = IMFSinkWriter_BeginWriting(e->writer);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"encoder: BeginWriting failed (0x%08lX)", (unsigned long)hr);
        IMFSinkWriter_Release(e->writer);
        e->writer = NULL;
        DeleteCriticalSection(&e->lock);
        DeleteFileW(path);
        return false;
    }
    return true;
}

static bool write_sample(Encoder *e, DWORD stream, const BYTE *data, DWORD size,
                         int64_t time_hns, int64_t duration_hns,
                         void (*fill)(Encoder *, const BYTE *, BYTE *)) {
    IMFMediaBuffer *buffer = NULL;
    IMFSample      *sample = NULL;

    HRESULT hr = MFCreateMemoryBuffer(size, &buffer);
    if (SUCCEEDED(hr)) {
        BYTE *dst = NULL;
        hr = IMFMediaBuffer_Lock(buffer, &dst, NULL, NULL);
        if (SUCCEEDED(hr)) {
            if (fill) fill(e, data, dst);
            else      memcpy(dst, data, size);
            IMFMediaBuffer_Unlock(buffer);
        }
    }
    if (SUCCEEDED(hr)) hr = IMFMediaBuffer_SetCurrentLength(buffer, size);
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = IMFSample_AddBuffer(sample, buffer);
    if (SUCCEEDED(hr)) hr = IMFSample_SetSampleTime(sample, time_hns);
    if (SUCCEEDED(hr)) hr = IMFSample_SetSampleDuration(sample, duration_hns);
    if (SUCCEEDED(hr)) {
        EnterCriticalSection(&e->lock);
        hr = IMFSinkWriter_WriteSample(e->writer, stream, sample);
        LeaveCriticalSection(&e->lock);
    }

    if (sample) IMFSample_Release(sample);
    if (buffer) IMFMediaBuffer_Release(buffer);
    if (FAILED(hr))
        log_msg(LOG_ERROR, L"encoder: WriteSample on stream %lu failed (0x%08lX)",
                (unsigned long)stream, (unsigned long)hr);
    return SUCCEEDED(hr);
}

static void fill_nv12(Encoder *e, const BYTE *bgra, BYTE *dst) {
    convert_bgra_to_nv12(bgra, e->width, e->height, dst);
}

bool encoder_write_video(Encoder *e, const BYTE *bgra, int64_t time_hns,
                         int64_t duration_hns) {
    DWORD size = e->nv12 ? (DWORD)(e->width * e->height * 3 / 2)
                         : (DWORD)(e->width * e->height * 4);
    bool ok = write_sample(e, e->video_stream, bgra, size, time_hns, duration_hns,
                           e->nv12 ? fill_nv12 : NULL);
    if (ok) e->video_samples++;
    return ok;
}

bool encoder_write_audio(Encoder *e, const int16_t *pcm, uint32_t frames,
                         int64_t time_hns, int64_t duration_hns) {
    if (!e->has_audio || frames == 0) return true;
    return write_sample(e, e->audio_stream, (const BYTE *)pcm, frames * 4,
                        time_hns, duration_hns, NULL);
}

bool encoder_close(Encoder *e) {
    if (!e->writer) return false;
    HRESULT hr = IMFSinkWriter_Finalize(e->writer);
    if (FAILED(hr))
        log_msg(LOG_ERROR, L"encoder: Finalize failed (0x%08lX)", (unsigned long)hr);
    IMFSinkWriter_Release(e->writer);
    e->writer = NULL;
    DeleteCriticalSection(&e->lock);
    return SUCCEEDED(hr);
}
