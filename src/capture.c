#include "mrecord.h"

#include <d3d11.h>
#include <dxgi1_5.h>

static const GUID MR_IID_IDXGIFactory1 =
    { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
static const GUID MR_IID_IDXGIOutput1 =
    { 0x00cddea8, 0x939b, 0x4b83, { 0xa3, 0x40, 0xa6, 0x85, 0x22, 0x66, 0x66, 0xcc } };
static const GUID MR_IID_IDXGIOutput5 =
    { 0x80a07424, 0xab52, 0x42eb, { 0x83, 0x3c, 0x0c, 0x42, 0xfd, 0x28, 0x2d, 0x98 } };
static const GUID MR_IID_ID3D11Texture2D =
    { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

static HRESULT find_output(const wchar_t *device, IDXGIAdapter1 **adapter_out,
                           IDXGIOutput **output_out, GRect *desktop) {
    IDXGIFactory1 *factory = NULL;
    HRESULT hr = CreateDXGIFactory1(&MR_IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr)) return hr;

    IDXGIAdapter1 *adapter = NULL;
    for (UINT a = 0; SUCCEEDED(IDXGIFactory1_EnumAdapters1(factory, a, &adapter)); a++) {
        IDXGIOutput *output = NULL;
        for (UINT o = 0; SUCCEEDED(IDXGIAdapter1_EnumOutputs(adapter, o, &output)); o++) {
            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(IDXGIOutput_GetDesc(output, &desc)) &&
                _wcsicmp(desc.DeviceName, device) == 0) {
                desktop->left   = desc.DesktopCoordinates.left;
                desktop->top    = desc.DesktopCoordinates.top;
                desktop->right  = desc.DesktopCoordinates.right;
                desktop->bottom = desc.DesktopCoordinates.bottom;
                *adapter_out = adapter;
                *output_out  = output;
                IDXGIFactory1_Release(factory);
                return S_OK;
            }
            IDXGIOutput_Release(output);
        }
        IDXGIAdapter1_Release(adapter);
    }

    IDXGIFactory1_Release(factory);
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT duplicate(IDXGIOutput *output, ID3D11Device *device,
                         IDXGIOutputDuplication **dup) {
    IDXGIOutput5 *out5 = NULL;
    if (SUCCEEDED(IDXGIOutput_QueryInterface(output, &MR_IID_IDXGIOutput5, (void **)&out5))) {
        DXGI_FORMAT formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM };
        HRESULT hr = IDXGIOutput5_DuplicateOutput1(out5, (IUnknown *)device, 0, 1,
                                                   formats, dup);
        IDXGIOutput5_Release(out5);
        if (SUCCEEDED(hr)) return hr;
        log_msg(LOG_WARN, L"capture: DuplicateOutput1 failed (0x%08lX), trying DuplicateOutput",
                (unsigned long)hr);
    }

    IDXGIOutput1 *out1 = NULL;
    HRESULT hr = IDXGIOutput_QueryInterface(output, &MR_IID_IDXGIOutput1, (void **)&out1);
    if (FAILED(hr)) return hr;
    hr = IDXGIOutput1_DuplicateOutput(out1, (IUnknown *)device, dup);
    IDXGIOutput1_Release(out1);
    return hr;
}

static void release_device(Capture *c) {
    if (c->staging) ID3D11Texture2D_Release(c->staging);
    if (c->dup)     IDXGIOutputDuplication_Release(c->dup);
    if (c->context) ID3D11DeviceContext_Release(c->context);
    if (c->device)  ID3D11Device_Release(c->device);
    c->staging = NULL;
    c->dup     = NULL;
    c->context = NULL;
    c->device  = NULL;
    c->tex_w   = 0;
    c->tex_h   = 0;
}

static bool open_device(Capture *c) {
    IDXGIAdapter1 *adapter = NULL;
    IDXGIOutput   *output  = NULL;
    GRect desktop;

    HRESULT hr = find_output(c->device_name, &adapter, &output, &desktop);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"capture: no DXGI output for %ls (0x%08lX)",
                c->device_name, (unsigned long)hr);
        return false;
    }

    hr = D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &c->device, NULL, &c->context);
    IDXGIAdapter1_Release(adapter);
    if (FAILED(hr)) {
        IDXGIOutput_Release(output);
        log_msg(LOG_ERROR, L"capture: D3D11CreateDevice failed (0x%08lX)", (unsigned long)hr);
        return false;
    }

    hr = duplicate(output, c->device, &c->dup);
    IDXGIOutput_Release(output);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"capture: duplicating %ls failed (0x%08lX)",
                c->device_name, (unsigned long)hr);
        release_device(c);
        return false;
    }

    DXGI_OUTDUPL_DESC dd;
    IDXGIOutputDuplication_GetDesc(c->dup, &dd);
    c->rotation = (int)dd.Rotation;
    c->output   = desktop;

    if (!grect_contains_rect(c->output, c->region)) {
        log_msg(LOG_ERROR, L"capture: the region no longer lies on %ls", c->device_name);
        release_device(c);
        return false;
    }
    return true;
}

bool capture_open(Capture *c, const wchar_t *device_name, GRect region) {
    ZeroMemory(c, sizeof *c);
    lstrcpynW(c->device_name, device_name, CCHDEVICENAME);
    c->region = region;
    c->width  = grect_width(region);
    c->height = grect_height(region);

    c->frame = (BYTE *)calloc((size_t)c->width * c->height, 4);
    if (!c->frame) return false;

    if (!open_device(c)) {
        free(c->frame);
        c->frame = NULL;
        return false;
    }
    return true;
}

bool capture_reopen(Capture *c) {
    release_device(c);
    return open_device(c);
}

static bool ensure_staging(Capture *c, ID3D11Texture2D *tex) {
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(tex, &desc);

    if (c->staging && (int)desc.Width == c->tex_w && (int)desc.Height == c->tex_h)
        return true;

    if (c->staging) ID3D11Texture2D_Release(c->staging);
    c->staging = NULL;

    desc.Usage          = D3D11_USAGE_STAGING;
    desc.BindFlags      = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags      = 0;
    desc.MipLevels      = 1;
    desc.ArraySize      = 1;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;

    HRESULT hr = ID3D11Device_CreateTexture2D(c->device, &desc, NULL, &c->staging);
    if (FAILED(hr)) {
        log_msg(LOG_ERROR, L"capture: staging texture failed (0x%08lX)", (unsigned long)hr);
        return false;
    }
    c->tex_w = (int)desc.Width;
    c->tex_h = (int)desc.Height;
    return true;
}

static bool copy_frame(Capture *c, ID3D11Texture2D *tex) {
    if (!ensure_staging(c, tex)) return false;

    ID3D11DeviceContext_CopyResource(c->context, (ID3D11Resource *)c->staging,
                                     (ID3D11Resource *)tex);

    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr = ID3D11DeviceContext_Map(c->context, (ID3D11Resource *)c->staging,
                                         0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) return false;

    int ox = c->region.left - c->output.left;
    int oy = c->region.top - c->output.top;
    const BYTE *src = (const BYTE *)map.pData;

    if (c->rotation == ROT_IDENTITY || c->rotation == ROT_UNSPECIFIED) {
        if (ox + c->width <= c->tex_w && oy + c->height <= c->tex_h) {
            for (int y = 0; y < c->height; y++)
                memcpy(c->frame + (size_t)y * c->width * 4,
                       src + (size_t)(oy + y) * map.RowPitch + (size_t)ox * 4,
                       (size_t)c->width * 4);
        }
    } else {
        for (int y = 0; y < c->height; y++) {
            BYTE *dst = c->frame + (size_t)y * c->width * 4;
            for (int x = 0; x < c->width; x++) {
                int tx, ty;
                geom_desktop_to_texture((Rotation)c->rotation, c->tex_w, c->tex_h,
                                        ox + x, oy + y, &tx, &ty);
                if (tx < 0 || ty < 0 || tx >= c->tex_w || ty >= c->tex_h) continue;
                memcpy(dst + x * 4, src + (size_t)ty * map.RowPitch + (size_t)tx * 4, 4);
            }
        }
    }

    ID3D11DeviceContext_Unmap(c->context, (ID3D11Resource *)c->staging, 0);
    return true;
}

CaptureStatus capture_next(Capture *c, UINT timeout_ms) {
    if (!c->dup) return CAPTURE_LOST;

    DXGI_OUTDUPL_FRAME_INFO info;
    IDXGIResource *res = NULL;
    HRESULT hr = IDXGIOutputDuplication_AcquireNextFrame(c->dup, timeout_ms, &info, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return CAPTURE_SAME;
    if (FAILED(hr)) {
        if (hr != DXGI_ERROR_ACCESS_LOST)
            log_msg(LOG_WARN, L"capture: AcquireNextFrame failed (0x%08lX)", (unsigned long)hr);
        return CAPTURE_LOST;
    }

    CaptureStatus status = CAPTURE_SAME;
    if (info.LastPresentTime.QuadPart != 0) {
        ID3D11Texture2D *tex = NULL;
        if (SUCCEEDED(IDXGIResource_QueryInterface(res, &MR_IID_ID3D11Texture2D, (void **)&tex))) {
            if (copy_frame(c, tex)) {
                c->have_frame = true;
                status = CAPTURE_NEW;
            }
            ID3D11Texture2D_Release(tex);
        }
    }

    IDXGIResource_Release(res);
    IDXGIOutputDuplication_ReleaseFrame(c->dup);
    return status;
}

void capture_close(Capture *c) {
    release_device(c);
    free(c->frame);
    c->frame = NULL;
}
