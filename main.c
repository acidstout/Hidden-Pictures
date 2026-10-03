/*  Hidden Pictures - turns an image into two noise sheets that reveal it when stacked.
 *  Pure Win32 + WIC + GDI. No third-party dependencies.
 *
 *  Copyright (C) 2026 Rekow IT
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *
 *  This program is free software: you can redistribute it and/or modify it under the terms of the
 *  GNU General Public License as published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY;
 *  see the LICENSE file for details.
 */
#define UNICODE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <initguid.h>
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <winspool.h>
#include <dlgs.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uuid.lib")

#define MINW(a, b) ((a) < (b) ? (a) : (b))
#define MAXW(a, b) ((a) > (b) ? (a) : (b))
#define CLAMPV(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define REL(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)

enum { IDC_OPEN = 101, IDC_MAGIC, IDC_SAVE, IDC_PRINT, IDC_SZ_DN, IDC_SZ_UP, IDC_DT_DN, IDC_DT_UP, IDC_THEME, IDC_PRINTER };
enum { TIMER_ANIM = 1 };
enum { DRAG_NONE, DRAG_SEL, DRAG_SHEET, DRAG_SLIDER };
enum { HL = 1, HR = 2, HT = 4, HB = 8, HMOVE = 16, HNEW = 32 };

/* ---------------------------------------------------------------- state */
typedef struct {
    COLORREF bg, card, well, text, sub, line, btn, btnHot, btnDown, accent, accentHot, accentDown, accentText, sheetLine;
} Theme;

static Theme    T;
static BOOL     g_dark;
static int      g_dpi = 96;
#define S(x) MulDiv((x), g_dpi, 96)

static HINSTANCE g_inst;
static HWND      g_hwnd, g_btn[10];
static int       g_themePref = -1;   /* -1 follow system, 0 light, 1 dark */
static BOOL      g_pendClick;        /* mouse-down on the source card that may turn out to be a click */
static LANGID    g_lang;
static HFONT     fUI, fBold, fTitle, fSmall, fIcon, fBig;
static IWICImagingFactory *g_wic;

/* source image */
static BYTE   *g_pix;                 /* premultiplied BGRA */
static int     g_iw, g_ih;
static IWICBitmap *g_wicbmp;
static WCHAR   g_path[MAX_PATH];
static HBITMAP g_prev;
static RECT    g_imgR;                /* preview rect in client coords */
static int     g_sx, g_sy, g_sw, g_sh;/* selection in image pixels */

/* settings */
static int g_sizeCm = 10, g_maxCm = 12, g_detail = 160;
static HGLOBAL g_hDevMode, g_hDevNames;   /* printer chosen in the app; NULL = Windows default printer */
static WCHAR g_prnText[300];
static int g_dist = 2;  /* apart distance in cells along the separation axis */

/* result */
static BYTE *g_A, *g_B;               /* 1 = black */
static int   g_cols, g_rows;
static BOOL  g_have, g_dirty;
static double g_t = 1.0;              /* 0 = together, 1 = apart */
static int   g_ax, g_ay, g_bx, g_by;  /* sheet origins, in cells */
static int   g_cs, g_dirV, g_ox, g_oy;
static DWORD g_animStart;

/* layout */
static RECT rcStatus, rcCard[2], rcSrc, rcHint, rcStageA, rcSlider, rcAbout;
static RECT rcLbl[2], rcVal[2];
static HBITMAP g_sDib; static DWORD *g_sBits; static int g_sw_, g_sh_;
static WCHAR g_status[400];

/* drag */
static int  g_drag, g_dragMode, g_dragSheet;
static POINT g_dragPt, g_grab;
static int  g_oSX, g_oSY, g_oSW, g_oSH;

static int Msg(HWND hw, const WCHAR *text, UINT flags);
static void RegSetInt(const WCHAR *name, int v);
static void SavePrinter(void);
static BOOL RegGetInt(const WCHAR *name, int *out);

/* ---------------------------------------------------------------- strings */
static const WCHAR *LoadStr(int id)
{
    static WCHAR *cache[64];
    if (id < 0 || id >= 64) return L"";
    if (cache[id]) return cache[id];
    HRSRC r = FindResourceExW(g_inst, RT_STRING, MAKEINTRESOURCEW((id >> 4) + 1), g_lang);
    if (!r) r = FindResourceExW(g_inst, RT_STRING, MAKEINTRESOURCEW((id >> 4) + 1), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    if (!r) return L"";
    const WCHAR *p = (const WCHAR *)LockResource(LoadResource(g_inst, r));
    if (!p) return L"";
    for (int i = 0; i < (id & 15); i++) p += 1 + *p;
    int n = *p++;
    WCHAR *s = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
    if (!s) return L"";
    memcpy(s, p, n * sizeof(WCHAR)); s[n] = 0;
    return cache[id] = s;
}

static void SetStatus(const WCHAR *s)
{
    wcsncpy_s(g_status, _countof(g_status), s, _TRUNCATE);
    if (g_hwnd) InvalidateRect(g_hwnd, &rcStatus, FALSE);
}

static void SetStatusFmt(int id, ...)
{
    WCHAR b[400]; va_list ap; va_start(ap, id);
    vswprintf_s(b, _countof(b), LoadStr(id), ap); va_end(ap);
    SetStatus(b);
}

/* ---------------------------------------------------------------- theme */
static void SetTheme(BOOL dark)
{
    g_dark = dark;
    if (!dark) {
        T = (Theme){ RGB(243,243,243), RGB(255,255,255), RGB(226,229,234), RGB(27,27,27), RGB(98,98,98), RGB(219,219,219),
                     RGB(251,251,251), RGB(244,244,244), RGB(235,235,235), RGB(0,103,192), RGB(22,117,200), RGB(0,84,160),
                     RGB(255,255,255), RGB(140,146,158) };
    } else {
        T = (Theme){ RGB(32,32,32), RGB(43,43,43), RGB(27,27,29), RGB(255,255,255), RGB(165,165,165), RGB(66,66,66),
                     RGB(56,56,56), RGB(66,66,66), RGB(48,48,48), RGB(76,194,255), RGB(100,204,255), RGB(58,168,228),
                     RGB(0,0,0), RGB(110,116,128) };
    }
}

static BOOL SystemUsesDark(void)
{
    DWORD v = 1, sz = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz);
    return v == 0;
}

typedef struct { DWORD attr; PVOID data; SIZE_T size; } WCAD;

static void ApplyTitleBar(HWND h)
{
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    typedef BOOL (WINAPI *pAllow)(HWND, BOOL);
    typedef int  (WINAPI *pMode)(int);
    typedef BOOL (WINAPI *pSWCA)(HWND, WCAD *);
    pAllow allow = ux ? (pAllow)GetProcAddress(ux, MAKEINTRESOURCEA(133)) : NULL;
    pMode  mode  = ux ? (pMode)GetProcAddress(ux, MAKEINTRESOURCEA(135)) : NULL;
    pSWCA  swca  = (pSWCA)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    BOOL v = g_dark;
    if (mode) mode(g_dark ? 2 : 3);
    if (allow) allow(h, g_dark);
    DwmSetWindowAttribute(h, 20, &v, sizeof v);
    DwmSetWindowAttribute(h, 19, &v, sizeof v);
    if (swca) { WCAD d = { 26, &v, sizeof v }; swca(h, &d); }
    BOOL act = (GetActiveWindow() == h);
    SendMessageW(h, WM_NCACTIVATE, !act, 0);
    SendMessageW(h, WM_NCACTIVATE, act, 0);
}

/* ---------------------------------------------------------------- fonts */
static void MakeFonts(void)
{
    HFONT *all[] = { &fUI, &fBold, &fTitle, &fSmall, &fIcon, &fBig };
    for (int i = 0; i < 6; i++) if (*all[i]) DeleteObject(*all[i]);
    #define MK(pt, w, face) CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, w, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face)
    fUI    = MK(9,  FW_NORMAL,   L"Segoe UI");
    fBold  = MK(9,  FW_SEMIBOLD, L"Segoe UI");
    fTitle = MK(11, FW_SEMIBOLD, L"Segoe UI");
    fSmall = MK(8,  FW_NORMAL,   L"Segoe UI");
    fIcon  = MK(11, FW_NORMAL,   L"Segoe MDL2 Assets");
    fBig   = MK(12, FW_NORMAL,   L"Segoe UI");
    #undef MK
}

/* ---------------------------------------------------------------- tiny helpers */
static RECT MkR(int l, int t, int r, int b) { RECT x = { l, t, r, b }; return x; }
static int  RW(const RECT *r) { return r->right - r->left; }
static int  RH(const RECT *r) { return r->bottom - r->top; }
#define PX(c) ((((c) & 0xFF) << 16) | ((c) & 0xFF00) | (((c) >> 16) & 0xFF))

static void Txt(HDC dc, HFONT f, COLORREF c, const WCHAR *s, RECT *r, UINT fmt)
{
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, r, fmt | DT_NOPREFIX);
}

static void RoundBox(HDC dc, RECT r, COLORREF fill, COLORREF border, int rad)
{
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HBRUSH b = CreateSolidBrush(fill);
    HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, b);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p); DeleteObject(b);
}

static void FillR(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c); FillRect(dc, r, b); DeleteObject(b);
}

/* ---------------------------------------------------------------- random */
static unsigned long long g_rng;
static unsigned int Rnd(void)
{
    g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27;
    return (unsigned int)((g_rng * 2685821657736338717ULL) >> 32);
}

/* ---------------------------------------------------------------- image loading */
static void FreeImage(void)
{
    free(g_pix); g_pix = NULL; REL(g_wicbmp);
    if (g_prev) { DeleteObject(g_prev); g_prev = NULL; }
    g_iw = g_ih = 0;
}

static BOOL LoadImageFile(const WCHAR *path)
{
    IWICBitmapDecoder *dec = NULL; IWICBitmapFrameDecode *fr = NULL; IWICFormatConverter *fc = NULL;
    IWICBitmapFlipRotator *rot = NULL; IWICBitmapSource *src = NULL; IWICBitmap *bm = NULL;
    BYTE *pix = NULL; BOOL ok = FALSE; UINT w = 0, h = 0;

    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(g_wic, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &fr))) goto done;

    UINT orient = 1;
    IWICMetadataQueryReader *q = NULL;
    if (SUCCEEDED(IWICBitmapFrameDecode_GetMetadataQueryReader(fr, &q)) && q) {
        PROPVARIANT pv; PropVariantInit(&pv);
        if (SUCCEEDED(IWICMetadataQueryReader_GetMetadataByName(q, L"/app1/ifd/{ushort=274}", &pv)) && pv.vt == VT_UI2) orient = pv.uiVal;
        PropVariantClear(&pv); REL(q);
    }

    if (FAILED(IWICImagingFactory_CreateFormatConverter(g_wic, &fc))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(fc, (IWICBitmapSource *)fr, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) goto done;
    src = (IWICBitmapSource *)fc; IUnknown_AddRef((IUnknown *)src);

    WICBitmapTransformOptions o = WICBitmapTransformRotate0;
    switch (orient) {
        case 2: o = WICBitmapTransformFlipHorizontal; break;
        case 3: o = WICBitmapTransformRotate180; break;
        case 4: o = WICBitmapTransformFlipVertical; break;
        case 5: o = WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal; break;
        case 6: o = WICBitmapTransformRotate90; break;
        case 7: o = WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal; break;
        case 8: o = WICBitmapTransformRotate270; break;
    }
    if (o != WICBitmapTransformRotate0) {
        if (SUCCEEDED(IWICImagingFactory_CreateBitmapFlipRotator(g_wic, &rot)) &&
            SUCCEEDED(IWICBitmapFlipRotator_Initialize(rot, src, o))) { REL(src); src = (IWICBitmapSource *)rot; rot = NULL; }
    }
    if (FAILED(IWICBitmapSource_GetSize(src, &w, &h)) || !w || !h || (unsigned long long)w * h > 400000000ULL) goto done;
    size_t bytes = (size_t)w * h * 4;
    pix = (BYTE *)malloc(bytes);
    if (!pix) goto done;
    if (FAILED(IWICBitmapSource_CopyPixels(src, NULL, w * 4, (UINT)bytes, pix))) goto done;
    if (FAILED(IWICImagingFactory_CreateBitmapFromMemory(g_wic, w, h, &GUID_WICPixelFormat32bppPBGRA, w * 4, (UINT)bytes, pix, &bm))) goto done;

    FreeImage();
    g_pix = pix; pix = NULL; g_wicbmp = bm; bm = NULL; g_iw = (int)w; g_ih = (int)h;
    g_sx = g_sy = 0; g_sw = g_iw; g_sh = g_ih;
    ok = TRUE;
done:
    free(pix); REL(bm); REL(rot); REL(src); REL(fc); REL(fr); REL(dec);
    return ok;
}

static void BuildPreview(void)
{
    if (g_prev) { DeleteObject(g_prev); g_prev = NULL; }
    if (!g_wicbmp || RW(&rcSrc) < 8 || RH(&rcSrc) < 8) return;
    int vw = RW(&rcSrc) - S(16), vh = RH(&rcSrc) - S(16);
    double sc = MINW((double)vw / g_iw, (double)vh / g_ih);
    if (sc > 2.0) sc = 2.0;
    int pw = MAXW(1, (int)(g_iw * sc)), ph = MAXW(1, (int)(g_ih * sc));
    g_imgR.left = rcSrc.left + (RW(&rcSrc) - pw) / 2; g_imgR.top = rcSrc.top + (RH(&rcSrc) - ph) / 2;
    g_imgR.right = g_imgR.left + pw; g_imgR.bottom = g_imgR.top + ph;

    IWICBitmapScaler *sc2 = NULL;
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(g_wic, &sc2))) return;
    if (SUCCEEDED(IWICBitmapScaler_Initialize(sc2, (IWICBitmapSource *)g_wicbmp, pw, ph, WICBitmapInterpolationModeFant))) {
        BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), pw, -ph, 1, 32, BI_RGB } };
        void *bits = NULL;
        HBITMAP hb = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (hb && SUCCEEDED(IWICBitmapScaler_CopyPixels(sc2, NULL, pw * 4, pw * ph * 4, (BYTE *)bits))) {
            DWORD *p = (DWORD *)bits; DWORD bg = PX(T.well);
            int br = (bg >> 16) & 255, bgc = (bg >> 8) & 255, bb = bg & 255;
            for (int i = 0; i < pw * ph; i++) {
                DWORD c = p[i]; int a = c >> 24;
                if (a < 255) {
                    int r = ((c >> 16) & 255) + (255 - a) * br / 255, g = ((c >> 8) & 255) + (255 - a) * bgc / 255, b = (c & 255) + (255 - a) * bb / 255;
                    c = (MINW(r, 255) << 16) | (MINW(g, 255) << 8) | MINW(b, 255);
                }
                p[i] = c & 0xFFFFFF;
            }
            g_prev = hb;
        } else if (hb) DeleteObject(hb);
    }
    REL(sc2);
}

/* ---------------------------------------------------------------- generation */
static void Generate(void)
{
    int sx = g_sx, sy = g_sy, sw = g_sw, sh = g_sh, N = g_detail, cols, rows;
    if (sw >= sh) { cols = N; rows = (int)((double)N * sh / sw + 0.5); }
    else          { rows = N; cols = (int)((double)N * sw / sh + 0.5); }
    cols = MAXW(cols, 1); rows = MAXW(rows, 1);
    int nc = cols * rows;

    float *sum = (float *)calloc(nc, sizeof(float));
    int   *cnt = (int *)calloc(nc, sizeof(int));
    int   *xt  = (int *)malloc(sw * sizeof(int));
    BYTE  *A = (BYTE *)malloc(nc), *B = (BYTE *)malloc(nc);
    float *v = (float *)malloc(nc * sizeof(float));
    if (!sum || !cnt || !xt || !A || !B || !v) goto out;

    for (int x = 0; x < sw; x++) xt[x] = (int)((long long)x * cols / sw);
    for (int y = 0; y < sh; y++) {
        int cy = (int)((long long)y * rows / sh);
        const BYTE *p = g_pix + ((size_t)(sy + y) * g_iw + sx) * 4;
        float *srow = sum + cy * cols; int *crow = cnt + cy * cols;
        for (int x = 0; x < sw; x++, p += 4) {
            int a = 255 - p[3];
            int b = MINW(p[0] + a, 255), g = MINW(p[1] + a, 255), r = MINW(p[2] + a, 255);
            srow[xt[x]] += (float)((b * 29 + g * 150 + r * 77) >> 8);
            crow[xt[x]]++;
        }
    }
    int hist[256] = { 0 };
    for (int i = 0; i < nc; i++) {
        float g = cnt[i] ? sum[i] / cnt[i] : 255.0f;
        v[i] = g; hist[(int)CLAMPV(g, 0, 255)]++;
    }
    /* auto-levels on the 1% / 99% percentiles */
    int lo = 0, hi = 255, acc = 0, t1 = nc / 100, t99 = nc - nc / 100;
    for (int i = 0; i < 256; i++) { acc += hist[i]; if (acc > t1) { lo = i; break; } }
    acc = 0;
    for (int i = 0; i < 256; i++) { acc += hist[i]; if (acc >= t99) { hi = i; break; } }
    if (hi - lo < 24) { lo = MAXW(0, lo - 12); hi = MINW(255, lo + 24); }
    for (int i = 0; i < nc; i++) v[i] = CLAMPV((v[i] - lo) * 255.0f / (hi - lo), 0.0f, 255.0f);

    /* Floyd-Steinberg -> 1 = black */
    BYTE *img = B; /* temporary use */
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            float o = v[y * cols + x], n = o < 128.0f ? 0.0f : 255.0f, e = o - n;
            img[y * cols + x] = (n == 0.0f);
            if (x + 1 < cols)               v[y * cols + x + 1]       += e * 7 / 16;
            if (y + 1 < rows) {
                if (x > 0)                  v[(y + 1) * cols + x - 1] += e * 3 / 16;
                                            v[(y + 1) * cols + x]     += e * 5 / 16;
                if (x + 1 < cols)           v[(y + 1) * cols + x + 1] += e * 1 / 16;
            }
        }
    }
    for (int i = 0; i < nc; i++) {
        BYTE im = img[i], a = (BYTE)(Rnd() & 1);
        A[i] = a; B[i] = (BYTE)(a ^ im);
    }
    free(g_A); free(g_B); g_A = A; g_B = B; A = B = NULL;
    g_cols = cols; g_rows = rows; g_have = TRUE; g_dirty = FALSE;
out:
    free(sum); free(cnt); free(xt); free(A); free(B); free(v);
}

/* ---------------------------------------------------------------- stage */
static void ApplyT(void)
{
    int dist = g_dist;
    int a = (int)floor((1.0 - g_t) * dist / 2.0 + 0.5), d = (int)floor(g_t * dist + 0.5);
    g_ax = g_ay = g_bx = g_by = 0;
    if (g_dirV) { g_ay = a; g_by = a + d; } else { g_ax = a; g_bx = a + d; }
}

static void StageGeom(void)
{
    if (!g_have) return;
    int sw = RW(&rcStageA), sh = RH(&rcStageA), gap = 2;
    int csH = MINW(sw / (2 * g_cols + gap), sh / g_rows);
    int csV = MINW(sw / g_cols, sh / (2 * g_rows + gap));
    if (csH >= csV) { g_dirV = 0; g_cs = csH; } else { g_dirV = 1; g_cs = csV; }
    g_cs = CLAMPV(g_cs, 1, 16);
    int aw = sw / g_cs, ah = sh / g_cs;
    if (csH < 1 && csV < 1) g_dirV = (ah - g_rows) > (aw - g_cols);   /* very fine grid: sheets may only partly separate */
    int size = g_dirV ? g_rows : g_cols, avail = g_dirV ? ah : aw;
    g_dist = CLAMPV(avail - size, 1, size + gap);
    int Wc = g_dirV ? g_cols : g_cols + g_dist, Hc = g_dirV ? g_rows + g_dist : g_rows;
    g_ox = MAXW(0, (sw - Wc * g_cs) / 2); g_oy = MAXW(0, (sh - Hc * g_cs) / 2);
    ApplyT();
}

static void PaintStage(HDC dc)
{
    int w = RW(&rcStageA), h = RH(&rcStageA);
    if (w < 4 || h < 4) return;
    if (!g_sDib || g_sw_ != w || g_sh_ != h) {
        if (g_sDib) DeleteObject(g_sDib);
        BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB } };
        g_sDib = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&g_sBits, NULL, 0);
        g_sw_ = w; g_sh_ = h;
    }
    if (!g_sDib) return;
    static int *cxA, *cxB; static int cxN;
    if (cxN < w) { free(cxA); free(cxB); cxA = (int *)malloc(w * sizeof(int)); cxB = (int *)malloc(w * sizeof(int)); cxN = w; }
    if (!cxA || !cxB) return;

    int cs = g_cs, ax0 = g_ox + g_ax * cs, ay0 = g_oy + g_ay * cs, bx0 = g_ox + g_bx * cs, by0 = g_oy + g_by * cs;
    int pw = g_cols * cs, ph = g_rows * cs;
    for (int x = 0; x < w; x++) {
        int ia = x - ax0, ib = x - bx0;
        cxA[x] = (ia >= 0 && ia < pw) ? ia / cs : -1;
        cxB[x] = (ib >= 0 && ib < pw) ? ib / cs : -1;
    }
    DWORD bg = PX(T.well), white = 0xFFFFFF, black = 0x14141C;
    for (int y = 0; y < h; y++) {
        int ra = y - ay0, rb = y - by0;
        int rowA = (ra >= 0 && ra < ph) ? ra / cs : -1, rowB = (rb >= 0 && rb < ph) ? rb / cs : -1;
        const BYTE *pa = rowA >= 0 ? g_A + rowA * g_cols : NULL, *pb = rowB >= 0 ? g_B + rowB * g_cols : NULL;
        DWORD *out = g_sBits + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            int ca = pa ? cxA[x] : -1, cb = pb ? cxB[x] : -1;
            if (cb >= 0 && pb[cb]) out[x] = black;
            else if (ca >= 0) out[x] = pa[ca] ? black : white;
            else if (cb >= 0) out[x] = white;
            else out[x] = bg;
        }
    }
    HDC m = CreateCompatibleDC(dc); HGDIOBJ old = SelectObject(m, g_sDib);
    BitBlt(dc, rcStageA.left, rcStageA.top, w, h, m, 0, 0, SRCCOPY);
    SelectObject(m, old); DeleteDC(m);

    HBRUSH fb = CreateSolidBrush(T.sheetLine);
    RECT a = MkR(rcStageA.left + ax0 - 1, rcStageA.top + ay0 - 1, rcStageA.left + ax0 + pw + 1, rcStageA.top + ay0 + ph + 1);
    RECT b = MkR(rcStageA.left + bx0 - 1, rcStageA.top + by0 - 1, rcStageA.left + bx0 + pw + 1, rcStageA.top + by0 + ph + 1);
    FrameRect(dc, &a, fb); FrameRect(dc, &b, fb); DeleteObject(fb);
}

static RECT SheetRect(int k) /* client coords */
{
    int sx = k ? g_bx : g_ax, sy = k ? g_by : g_ay;
    int l = rcStageA.left + g_ox + sx * g_cs, t = rcStageA.top + g_oy + sy * g_cs;
    return MkR(l, t, l + g_cols * g_cs, t + g_rows * g_cs);
}

/* slider geometry */
static void SliderGeom(int *tl, int *tr, int *cy)
{
    *tl = rcSlider.left + S(110); *tr = rcSlider.right - S(110); *cy = (rcSlider.top + rcSlider.bottom) / 2;
}

static void SetTFromX(HWND hw, int x)
{
    int tl, tr, cy; SliderGeom(&tl, &tr, &cy);
    g_t = CLAMPV((double)(x - tl) / (tr - tl), 0.0, 1.0);
    ApplyT();
    InvalidateRect(hw, &rcStageA, FALSE); InvalidateRect(hw, &rcSlider, FALSE);
}

/* ---------------------------------------------------------------- layout */
static void SetBtnText(void)
{
    SetWindowTextW(g_btn[0], LoadStr(IDS_BTN_OPEN));
    SetWindowTextW(g_btn[1], LoadStr(IDS_BTN_MAGIC));
    SetWindowTextW(g_btn[2], LoadStr(IDS_BTN_SAVE));
    SetWindowTextW(g_btn[3], LoadStr(IDS_BTN_PRINT));
    SetWindowTextW(g_btn[4], L"\x2212"); SetWindowTextW(g_btn[5], L"+");
    SetWindowTextW(g_btn[6], L"\x2212"); SetWindowTextW(g_btn[7], L"+");
    SetWindowTextW(g_btn[8], LoadStr(g_dark ? IDS_THEME_DARK : IDS_THEME_LIGHT));
}

static void Layout(HWND hw)
{
    RECT c; GetClientRect(hw, &c);
    int W = c.right, H = c.bottom, m = S(16), tbH = S(72), stH = S(30), pad = S(12);
    rcStatus = MkR(0, H - stH, W, H);
    int top = tbH, bot = H - stH - m, lw = (int)((W - 3 * m) * 0.38);
    rcCard[0] = MkR(m, top, m + lw, bot);
    rcCard[1] = MkR(m + lw + m, top, W - m, bot);

    int bw[4] = { S(150), S(160), S(130), S(120) }, x = m, by = S(16), bh = S(40);
    for (int i = 0; i < 4; i++) { MoveWindow(g_btn[i], x, by, bw[i], bh, TRUE); x += bw[i] + S(8); }

    MoveWindow(g_btn[8], W - m - S(150), by, S(150), bh, TRUE);

    RECT *c0 = &rcCard[0], *c1 = &rcCard[1];
    rcSrc = MkR(c0->left + pad, c0->top + S(44), c0->right - pad, c0->bottom - S(112));
    RECT row = MkR(c0->left + pad, c0->bottom - S(100), c0->right - pad, c0->bottom - S(60));
    MoveWindow(g_btn[9], c0->left + pad, c0->bottom - S(48), RW(&row), S(36), TRUE);
    int half = RW(&row) / 2, bs = S(32), lblw = S(72), valw = S(48);
    for (int k = 0; k < 2; k++) {
        int x0 = row.left + k * half, cy = (row.top + row.bottom) / 2;
        rcLbl[k] = MkR(x0, row.top, x0 + lblw, row.bottom);
        MoveWindow(g_btn[4 + 2 * k], x0 + lblw, cy - bs / 2, bs, bs, TRUE);
        rcVal[k] = MkR(x0 + lblw + bs, row.top, x0 + lblw + bs + valw, row.bottom);
        MoveWindow(g_btn[5 + 2 * k], x0 + lblw + bs + valw, cy - bs / 2, bs, bs, TRUE);
    }
    rcHint    = MkR(c1->left + pad, c1->top + S(44), c1->right - pad, c1->top + S(66));
    rcStageA  = MkR(c1->left + pad, c1->top + S(72), c1->right - pad, c1->bottom - S(64));
    rcSlider  = MkR(c1->left + pad, c1->bottom - S(52), c1->right - pad, c1->bottom - S(12));

    HDC dc = GetDC(hw); SelectObject(dc, fSmall);
    SIZE sz; const WCHAR *al = LoadStr(IDS_ABOUT_LINK);
    GetTextExtentPoint32W(dc, al, (int)wcslen(al), &sz); ReleaseDC(hw, dc);
    rcAbout = MkR(W - m - sz.cx - S(8), rcStatus.top, W - m + S(4), rcStatus.bottom);

    BuildPreview();
    StageGeom();
}

static void UpdateButtons(void)
{
    EnableWindow(g_btn[1], g_pix != NULL);
    EnableWindow(g_btn[2], g_have); EnableWindow(g_btn[3], g_have);
    for (int i = 0; i < 4; i++) InvalidateRect(g_btn[i], NULL, FALSE);
}

/* ---------------------------------------------------------------- selection */
static RECT SelView(void)
{
    double k = (double)RW(&g_imgR) / g_iw, ky = (double)RH(&g_imgR) / g_ih;
    return MkR(g_imgR.left + (int)(g_sx * k + 0.5), g_imgR.top + (int)(g_sy * ky + 0.5),
               g_imgR.left + (int)((g_sx + g_sw) * k + 0.5), g_imgR.top + (int)((g_sy + g_sh) * ky + 0.5));
}

static int HitSel(int px, int py)
{
    if (!g_pix || !g_prev) return 0;
    RECT s = SelView(); int tol = S(8), mask = 0;
    if (py >= s.top - tol && py <= s.bottom + tol) { if (abs(px - s.left) <= tol) mask |= HL; if (abs(px - s.right) <= tol) mask |= HR; }
    if (px >= s.left - tol && px <= s.right + tol) { if (abs(py - s.top) <= tol) mask |= HT; if (abs(py - s.bottom) <= tol) mask |= HB; }
    if (mask) return mask;
    if (px >= s.left && px < s.right && py >= s.top && py < s.bottom) return HMOVE;
    POINT p = { px, py };
    if (PtInRect(&g_imgR, p)) return HNEW;
    return 0;
}

static void ViewToImg(int vx, int vy, int *ix, int *iy)
{
    *ix = CLAMPV((int)((double)(vx - g_imgR.left) * g_iw / RW(&g_imgR) + 0.5), 0, g_iw);
    *iy = CLAMPV((int)((double)(vy - g_imgR.top) * g_ih / RH(&g_imgR) + 0.5), 0, g_ih);
}

static void DragSel(HWND hw, int vx, int vy)
{
    int ix, iy, minS = MAXW(8, MINW(g_iw, g_ih) / 100); ViewToImg(vx, vy, &ix, &iy);
    int sx0, sy0; ViewToImg(g_dragPt.x, g_dragPt.y, &sx0, &sy0);
    int dx = ix - sx0, dy = iy - sy0;
    int l = g_oSX, t = g_oSY, r = g_oSX + g_oSW, b = g_oSY + g_oSH;
    if (g_dragMode == HNEW) {
        l = MINW(sx0, ix); r = MAXW(sx0, ix); t = MINW(sy0, iy); b = MAXW(sy0, iy);
        if (r - l < minS || b - t < minS) goto done;
    } else if (g_dragMode == HMOVE) {
        dx = CLAMPV(dx, -l, g_iw - r); dy = CLAMPV(dy, -t, g_ih - b);
        l += dx; r += dx; t += dy; b += dy;
    } else {
        if (g_dragMode & HL) l = CLAMPV(l + dx, 0, r - minS);
        if (g_dragMode & HR) r = CLAMPV(r + dx, l + minS, g_iw);
        if (g_dragMode & HT) t = CLAMPV(t + dy, 0, b - minS);
        if (g_dragMode & HB) b = CLAMPV(b + dy, t + minS, g_ih);
    }
    g_sx = l; g_sy = t; g_sw = r - l; g_sh = b - t;
done:
    InvalidateRect(hw, &rcSrc, FALSE); InvalidateRect(hw, &rcCard[0], FALSE);
}

/* ---------------------------------------------------------------- paint */
static void DrawCard(HDC dc, RECT r, const WCHAR *title)
{
    RoundBox(dc, r, T.card, T.line, S(8));
    RECT tr = MkR(r.left + S(16), r.top + S(10), r.right - S(16), r.top + S(38));
    Txt(dc, fTitle, T.text, title, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void DrawSelection(HDC dc)
{
    RECT s = SelView(), im = g_imgR;
    HDC d = CreateCompatibleDC(dc); HBITMAP hb = CreateCompatibleBitmap(dc, 1, 1); HGDIOBJ old = SelectObject(d, hb);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 120, 0 };
    RECT q[4] = { MkR(im.left, im.top, im.right, s.top), MkR(im.left, s.bottom, im.right, im.bottom),
                  MkR(im.left, s.top, s.left, s.bottom), MkR(s.right, s.top, im.right, s.bottom) };
    for (int i = 0; i < 4; i++) if (RW(&q[i]) > 0 && RH(&q[i]) > 0)
        AlphaBlend(dc, q[i].left, q[i].top, RW(&q[i]), RH(&q[i]), d, 0, 0, 1, 1, bf);
    SelectObject(d, old); DeleteObject(hb); DeleteDC(d);

    HPEN wp = CreatePen(PS_SOLID, 1, RGB(255,255,255)), ap = CreatePen(PS_SOLID, S(2), T.accent);
    HGDIOBJ op = SelectObject(dc, ap), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, s.left, s.top, s.right, s.bottom);
    SelectObject(dc, wp); Rectangle(dc, s.left + S(2), s.top + S(2), s.right - S(2), s.bottom - S(2));
    int hs = S(5), mx = (s.left + s.right) / 2, my = (s.top + s.bottom) / 2;
    POINT hp[8] = { {s.left,s.top},{mx,s.top},{s.right,s.top},{s.left,my},{s.right,my},{s.left,s.bottom},{mx,s.bottom},{s.right,s.bottom} };
    HBRUSH wb = CreateSolidBrush(RGB(255,255,255)); SelectObject(dc, wb); SelectObject(dc, ap);
    for (int i = 0; i < 8; i++) Rectangle(dc, hp[i].x - hs, hp[i].y - hs, hp[i].x + hs + 1, hp[i].y + hs + 1);
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(wp); DeleteObject(ap); DeleteObject(wb);
}

static void DrawSlider(HDC dc)
{
    int tl, tr, cy; SliderGeom(&tl, &tr, &cy);
    COLORREF tc = g_have ? T.text : T.sub;
    RECT l = MkR(rcSlider.left, rcSlider.top, tl - S(14), rcSlider.bottom), r = MkR(tr + S(14), rcSlider.top, rcSlider.right, rcSlider.bottom);
    Txt(dc, fUI, tc, LoadStr(IDS_SL_TOGETHER), &l, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    Txt(dc, fUI, tc, LoadStr(IDS_SL_APART), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int th = S(4), tx = tl + (int)(g_t * (tr - tl));
    RECT track = MkR(tl, cy - th / 2, tr, cy + th / 2 + 1);
    RoundBox(dc, track, T.line, T.line, th / 2);
    if (!g_have) return;
    RECT fill = MkR(tl, cy - th / 2, tx, cy + th / 2 + 1);
    if (RW(&fill) > 0) RoundBox(dc, fill, T.accent, T.accent, th / 2);
    int rr = S(10);
    HPEN p = CreatePen(PS_SOLID, 1, T.accent); HBRUSH b = CreateSolidBrush(T.card);
    HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, b);
    Ellipse(dc, tx - rr, cy - rr, tx + rr + 1, cy + rr + 1);
    HBRUSH ab = CreateSolidBrush(T.accent); SelectObject(dc, ab);
    Ellipse(dc, tx - rr + S(5), cy - rr + S(5), tx + rr - S(4), cy + rr - S(4));
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p); DeleteObject(b); DeleteObject(ab);
}

static void DrawStepper(HDC dc, int k, const WCHAR *label, const WCHAR *value)
{
    RECT l = rcLbl[k], v = rcVal[k];
    Txt(dc, fUI, T.sub, label, &l, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Txt(dc, fBold, T.text, value, &v, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void OnPaint(HWND hw)
{
    PAINTSTRUCT ps; HDC wdc = BeginPaint(hw, &ps);
    RECT cr; GetClientRect(hw, &cr);
    static HBITMAP bb; static int bw, bh;
    if (!bb || bw != cr.right || bh != cr.bottom) {
        if (bb) DeleteObject(bb);
        bb = CreateCompatibleBitmap(wdc, cr.right, cr.bottom); bw = cr.right; bh = cr.bottom;
    }
    HDC dc = CreateCompatibleDC(wdc); HGDIOBJ oldbb = SelectObject(dc, bb);
    FillR(dc, &cr, T.bg);

    /* source card */
    DrawCard(dc, rcCard[0], LoadStr(IDS_CARD1));
    {
        WCHAR info[128]; RECT ir = MkR(rcCard[0].left + S(16), rcCard[0].top + S(10), rcCard[0].right - S(16), rcCard[0].top + S(38));
        if (g_pix) { swprintf_s(info, 128, LoadStr(IDS_SEL_INFO), g_iw, g_ih, g_sw, g_sh); Txt(dc, fSmall, T.sub, info, &ir, DT_RIGHT | DT_VCENTER | DT_SINGLELINE); }
        RoundBox(dc, rcSrc, T.well, T.well, S(6));
        if (g_pix && g_prev) {
            HDC m = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(m, g_prev);
            BitBlt(dc, g_imgR.left, g_imgR.top, RW(&g_imgR), RH(&g_imgR), m, 0, 0, SRCCOPY);
            SelectObject(m, o); DeleteDC(m);
            DrawSelection(dc);
        } else {
            RECT h = rcSrc; Txt(dc, fBig, T.sub, LoadStr(IDS_HINT_OPEN), &h, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        WCHAR v[32];
        swprintf_s(v, 32, L"%d %ls", g_sizeCm, LoadStr(IDS_UNIT_CM)); DrawStepper(dc, 0, LoadStr(IDS_SET_SIZE), v);
        swprintf_s(v, 32, L"%d", g_detail);                         DrawStepper(dc, 1, LoadStr(IDS_SET_DETAIL), v);
    }

    /* result card */
    DrawCard(dc, rcCard[1], LoadStr(IDS_CARD2));
    {
        RoundBox(dc, MkR(rcStageA.left, rcStageA.top, rcStageA.right, rcStageA.bottom), T.well, T.well, S(6));
        if (g_have) {
            RECT h = rcHint; Txt(dc, fUI, T.sub, LoadStr(IDS_HINT_DRAG), &h, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            PaintStage(dc);
        } else {
            RECT h = rcStageA; Txt(dc, fBig, T.sub, LoadStr(IDS_HINT_RESULT), &h, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        DrawSlider(dc);
    }

    /* status bar */
    {
        HPEN p = CreatePen(PS_SOLID, 1, T.line); HGDIOBJ o = SelectObject(dc, p);
        MoveToEx(dc, 0, rcStatus.top, NULL); LineTo(dc, cr.right, rcStatus.top); SelectObject(dc, o); DeleteObject(p);
        RECT s = MkR(S(16), rcStatus.top, rcAbout.left - S(12), rcStatus.bottom);
        Txt(dc, fSmall, T.sub, g_status, &s, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT a = rcAbout; Txt(dc, fSmall, T.accent, LoadStr(IDS_ABOUT_LINK), &a, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldbb); DeleteDC(dc);
    EndPaint(hw, &ps);
}

/* ---------------------------------------------------------------- owner-drawn buttons */
static LRESULT CALLBACK BtnProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    switch (m) {
    case WM_MOUSEMOVE:
        if (!GetPropW(h, L"hot")) {
            SetPropW(h, L"hot", (HANDLE)1);
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE: RemovePropW(h, L"hot"); InvalidateRect(h, NULL, FALSE); break;
    case WM_ERASEBKGND: return 1;
    case WM_NCDESTROY: RemovePropW(h, L"hot"); RemoveWindowSubclass(h, BtnProc, 0); break;
    }
    return DefSubclassProc(h, m, w, l);
}

static void DrawButton(const DRAWITEMSTRUCT *di)
{
    HDC dc = di->hDC; RECT r = di->rcItem; int id = (int)di->CtlID;
    BOOL dis = di->itemState & ODS_DISABLED, down = di->itemState & ODS_SELECTED, hot = GetPropW(di->hwndItem, L"hot") != NULL;
    BOOL accent = (id == IDC_MAGIC);
    COLORREF fill, border, tc;
    if (accent) {
        fill = dis ? T.line : down ? T.accentDown : hot ? T.accentHot : T.accent;
        border = fill; tc = dis ? T.sub : T.accentText;
    } else {
        fill = down ? T.btnDown : hot && !dis ? T.btnHot : T.btn; border = T.line; tc = dis ? T.sub : T.text;
    }
    FillR(dc, &r, T.bg);
    BOOL stepper = id >= IDC_SZ_DN && id <= IDC_DT_UP;
    if (stepper) { COLORREF c = (r.right - r.left) > 0 ? T.card : T.bg; FillR(dc, &r, c); }
    RoundBox(dc, r, fill, border, S(6));

    WCHAR txt[320]; GetWindowTextW(di->hwndItem, txt, 320);
    if (id == IDC_PRINTER) {
        RECT gr = MkR(r.left + S(14), r.top, r.left + S(40), r.bottom), tr2 = MkR(r.left + S(46), r.top, r.right - S(12), r.bottom);
        Txt(dc, fIcon, tc, L"\xE749", &gr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Txt(dc, fUI, tc, txt, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (stepper) {
        SelectObject(dc, fBold); Txt(dc, fBold, tc, txt, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        const WCHAR *glyph = id == IDC_OPEN ? L"\xE838" : id == IDC_MAGIC ? L"\xE945" : id == IDC_SAVE ? L"\xE74E" : id == IDC_THEME ? (g_dark ? L"\xE708" : L"\xE706") : L"\xE749";
        SIZE gs, ts; HGDIOBJ o = SelectObject(dc, fIcon); GetTextExtentPoint32W(dc, glyph, 1, &gs);
        SelectObject(dc, fBold); GetTextExtentPoint32W(dc, txt, (int)wcslen(txt), &ts); SelectObject(dc, o);
        int gap = S(10), x = (r.left + r.right - (gs.cx + gap + ts.cx)) / 2;
        RECT gr = MkR(x, r.top, x + gs.cx, r.bottom), tr2 = MkR(x + gs.cx + gap, r.top, x + gs.cx + gap + ts.cx + 2, r.bottom);
        Txt(dc, fIcon, tc, glyph, &gr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Txt(dc, fBold, tc, txt, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if ((di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT)) {
        RECT f = r; InflateRect(&f, -S(3), -S(3));
        HPEN p = CreatePen(PS_SOLID, S(2), accent ? T.accentText : T.accent);
        HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, f.left, f.top, f.right, f.bottom, S(6), S(6));
        SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p);
    }
}

/* ---------------------------------------------------------------- file dialogs */
enum { FMT_PNG, FMT_TIFF, FMT_BMP, FMT_GIF };
static const WCHAR *k_ext[4] = { L".png", L".tif", L".bmp", L".gif" };

static int FmtFromExt(const WCHAR *path)
{
    const WCHAR *e = PathFindExtensionW(path);
    if (!_wcsicmp(e, L".png")) return FMT_PNG;
    if (!_wcsicmp(e, L".tif") || !_wcsicmp(e, L".tiff")) return FMT_TIFF;
    if (!_wcsicmp(e, L".bmp")) return FMT_BMP;
    if (!_wcsicmp(e, L".gif")) return FMT_GIF;
    return -1;
}

/* "Label (*.a, *.b)" - the file type list shows the extensions, not just the name */
static void TypeLabel(WCHAR *out, size_t n, const WCHAR *name, const WCHAR *patterns)
{
    WCHAR shown[512]; size_t k = 0;
    for (const WCHAR *p = patterns; *p && k + 3 < _countof(shown); p++) {
        if (*p == L';') { shown[k++] = L','; shown[k++] = L' '; } else shown[k++] = *p;
    }
    shown[k] = 0;
    swprintf_s(out, n, L"%ls (%ls)", name, shown);
}

/* Both dialogs use the same Windows common item dialog, so they look identical. */
static BOOL ShowFileDialog(HWND hw, BOOL save, const WCHAR *defName, int *fmt, WCHAR *out)
{
    static const WCHAR *k_imgPat = L"*.jpg;*.jpeg;*.jfif;*.png;*.gif;*.bmp;*.dib;*.tif;*.tiff;*.jxr;*.wdp;*.hdp;*.ico;*.heic;*.heif;*.webp;*.avif";
    IFileDialog *d = NULL; IShellItem *res = NULL, *folder = NULL; BOOL ok = FALSE;
    WCHAR l[8][640]; COMDLG_FILTERSPEC fs[5]; UINT n = 0;
    HRESULT hr = CoCreateInstance(save ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  save ? &IID_IFileSaveDialog : &IID_IFileOpenDialog, (void **)&d);
    if (FAILED(hr)) return FALSE;

    if (save) {
        TypeLabel(l[0], 640, LoadStr(IDS_FILT_PNG),  L"*.png");          fs[n].pszName = l[0]; fs[n++].pszSpec = L"*.png";
        TypeLabel(l[1], 640, LoadStr(IDS_FILT_TIFF), L"*.tif;*.tiff");   fs[n].pszName = l[1]; fs[n++].pszSpec = L"*.tif;*.tiff";
        TypeLabel(l[2], 640, LoadStr(IDS_FILT_BMP),  L"*.bmp");          fs[n].pszName = l[2]; fs[n++].pszSpec = L"*.bmp";
        TypeLabel(l[3], 640, LoadStr(IDS_FILT_GIF),  L"*.gif");          fs[n].pszName = l[3]; fs[n++].pszSpec = L"*.gif";
    } else {
        TypeLabel(l[0], 640, LoadStr(IDS_FILT_IMAGES), k_imgPat);        fs[n].pszName = l[0]; fs[n++].pszSpec = k_imgPat;
        TypeLabel(l[1], 640, LoadStr(IDS_FILT_ALL), L"*.*");             fs[n].pszName = l[1]; fs[n++].pszSpec = L"*.*";
    }
    IFileDialog_SetFileTypes(d, n, fs);
    IFileDialog_SetFileTypeIndex(d, 1);
    IFileDialog_SetOptions(d, FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    if (save) { IFileDialog_SetDefaultExtension(d, L"png"); IFileDialog_SetFileName(d, defName); }
    if (g_path[0]) {                                  /* start in the folder of the current picture */
        WCHAR dir[MAX_PATH]; wcscpy_s(dir, MAX_PATH, g_path); PathRemoveFileSpecW(dir);
        if (SUCCEEDED(SHCreateItemFromParsingName(dir, NULL, &IID_IShellItem, (void **)&folder))) IFileDialog_SetFolder(d, folder);
    }
    if (SUCCEEDED(IFileDialog_Show(d, hw)) && SUCCEEDED(IFileDialog_GetResult(d, &res))) {
        WCHAR *p = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(res, SIGDN_FILESYSPATH, &p)) && p) {
            wcscpy_s(out, MAX_PATH, p); CoTaskMemFree(p); ok = TRUE;
            if (save) {
                UINT idx = 1; IFileDialog_GetFileTypeIndex(d, &idx);
                int f = FmtFromExt(out);
                if (f < 0) {                          /* no (known) extension: take it from the selected file type */
                    f = CLAMPV((int)idx - 1, 0, 3);
                    wcscat_s(out, MAX_PATH, k_ext[f]);
                    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) {
                        WCHAR q[MAX_PATH + 100]; swprintf_s(q, _countof(q), LoadStr(IDS_CONFIRM_REPLACE), PathFindFileNameW(out));
                        if (Msg(hw, q, MB_YESNO | MB_ICONQUESTION) != IDYES) ok = FALSE;
                    }
                }
                *fmt = f;
            }
        }
    }
    REL(res); REL(folder); REL(d);
    return ok;
}

static BOOL OpenDialog(HWND hw, WCHAR *out) { int f; return ShowFileDialog(hw, FALSE, NULL, &f, out); }

static BOOL SaveDialog(HWND hw, WCHAR *out, int *fmt)
{
    WCHAR base[MAX_PATH] = L"hidden", name[MAX_PATH + 16];
    if (g_path[0]) { wcscpy_s(base, MAX_PATH, PathFindFileNameW(g_path)); PathRemoveExtensionW(base); }
    swprintf_s(name, _countof(name), L"%ls_hidden.png", base);
    return ShowFileDialog(hw, TRUE, name, fmt, out);
}

/* ---------------------------------------------------------------- PNG export */
static BOOL EncodeImage(int fmt, int tiffComp, const BYTE *buf, int W, int H, int stride, double dpi, BYTE **outData, DWORD *outSize);

static BOOL SaveImage(const WCHAR *path, int fmt)
{
    int N = MAXW(g_cols, g_rows);
    int P = MAXW(4, (int)(g_sizeCm / 2.54 * 600.0 / N + 0.5));
    int M = P, gap = 8 * P;
    int W = g_cols * P + 2 * M, H = 2 * g_rows * P + gap + 2 * M;
    int stride = (W + 7) / 8;
    BYTE *buf = (BYTE *)malloc((size_t)stride * H);
    if (!buf) return FALSE;
    memset(buf, 0xFF, (size_t)stride * H);                /* 1 = white */
    for (int k = 0; k < 2; k++) {
        const BYTE *src = k ? g_B : g_A;
        int y0 = M + k * (g_rows * P + gap);
        for (int y = 0; y < g_rows * P; y++) {
            const BYTE *row = src + (y / P) * g_cols; BYTE *o = buf + (size_t)(y0 + y) * stride;
            for (int x = 0; x < g_cols * P; x++)
                if (row[x / P]) { int xx = M + x; o[xx >> 3] &= (BYTE)~(0x80 >> (xx & 7)); }
        }
    }
    /* dashed cut line in the gap */
    int cy = M + g_rows * P + gap / 2, th = MAXW(1, P / 4);
    for (int t = 0; t < th; t++) {
        BYTE *o = buf + (size_t)(cy - th / 2 + t) * stride;
        for (int x = 0; x < W; x++) if ((x / (3 * P)) % 2 == 0) o[x >> 3] &= (BYTE)~(0x80 >> (x & 7));
    }

    double dpi = (double)P * N / (g_sizeCm / 2.54);
    BYTE *data = NULL; DWORD size = 0; BOOL ok = FALSE;
    if (fmt == FMT_TIFF) {
        /* lossless TIFF: try LZW and PackBits, keep whichever is smaller for this picture */
        BYTE *d2 = NULL; DWORD s2 = 0;
        BOOL a = EncodeImage(fmt, WICTiffCompressionLZW, buf, W, H, stride, dpi, &data, &size);
        BOOL b = EncodeImage(fmt, WICTiffCompressionRLE, buf, W, H, stride, dpi, &d2, &s2);
        if (b && (!a || s2 < size)) { free(data); data = d2; size = s2; a = TRUE; } else free(d2);
        ok = a;
    } else ok = EncodeImage(fmt, 0, buf, W, H, stride, dpi, &data, &size);
    free(buf);
    if (ok) {
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD wr = 0;
        ok = f != INVALID_HANDLE_VALUE && WriteFile(f, data, size, &wr, NULL) && wr == size;
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        if (!ok) DeleteFileW(path);
    }
    free(data);
    return ok;
}

/* Encodes the 1-bit sheet (bit 1 = white) into memory. tiffComp is a WICTiffCompressionOption for TIFF. */
static BOOL EncodeImage(int fmt, int tiffComp, const BYTE *buf, int W, int H, int stride, double dpi, BYTE **outData, DWORD *outSize)
{
    static const GUID *cont[4] = { &GUID_ContainerFormatPng, &GUID_ContainerFormatTiff, &GUID_ContainerFormatBmp, &GUID_ContainerFormatGif };
    IStream *st = NULL; IWICBitmapEncoder *enc = NULL; IWICBitmapFrameEncode *fr = NULL; IPropertyBag2 *pb = NULL; IWICPalette *pal = NULL;
    BOOL ok = FALSE, indexed = (fmt == FMT_BMP || fmt == FMT_GIF);
    WICPixelFormatGUID pf = fmt == FMT_GIF ? GUID_WICPixelFormat8bppIndexed : indexed ? GUID_WICPixelFormat1bppIndexed : GUID_WICPixelFormatBlackWhite, want = pf;
    BYTE *px8 = NULL;

    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &st))) goto done;
    if (FAILED(IWICImagingFactory_CreateEncoder(g_wic, cont[fmt], NULL, &enc))) goto done;
    if (FAILED(IWICBitmapEncoder_Initialize(enc, st, WICBitmapEncoderNoCache))) goto done;
    if (indexed) {                                 /* palette: index 0 = black, index 1 = white */
        WICColor c[2] = { 0xFF000000u, 0xFFFFFFFFu };
        if (FAILED(IWICImagingFactory_CreatePalette(g_wic, &pal)) || FAILED(IWICPalette_InitializeCustom(pal, c, 2))) goto done;
        if (fmt == FMT_GIF) IWICBitmapEncoder_SetPalette(enc, pal);
    }
    if (FAILED(IWICBitmapEncoder_CreateNewFrame(enc, &fr, &pb))) goto done;
    if (fmt == FMT_TIFF && pb) {
        PROPBAG2 opt; VARIANT v; memset(&opt, 0, sizeof opt); VariantInit(&v);
        opt.pstrName = L"TiffCompressionMethod"; v.vt = VT_UI1; v.bVal = (BYTE)tiffComp;
        if (FAILED(IPropertyBag2_Write(pb, 1, &opt, &v))) goto done;
    }
    if (FAILED(IWICBitmapFrameEncode_Initialize(fr, pb))) goto done;
    if (FAILED(IWICBitmapFrameEncode_SetSize(fr, W, H))) goto done;
    if (fmt != FMT_GIF) IWICBitmapFrameEncode_SetResolution(fr, dpi, dpi);
    if (FAILED(IWICBitmapFrameEncode_SetPixelFormat(fr, &pf)) || !IsEqualGUID(&pf, &want)) goto done;
    if (pal && FAILED(IWICBitmapFrameEncode_SetPalette(fr, pal))) goto done;
    if (fmt == FMT_GIF) {                          /* GIF encoder wants 8-bit indices */
        px8 = (BYTE *)malloc((size_t)W * H);
        if (!px8) goto done;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) px8[(size_t)y * W + x] = (buf[(size_t)y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
        if (FAILED(IWICBitmapFrameEncode_WritePixels(fr, H, W, W * H, px8))) goto done;
    } else if (FAILED(IWICBitmapFrameEncode_WritePixels(fr, H, stride, stride * H, (BYTE *)buf))) goto done;
    if (FAILED(IWICBitmapFrameEncode_Commit(fr))) goto done;
    if (FAILED(IWICBitmapEncoder_Commit(enc))) goto done;
    {
        STATSTG ss; LARGE_INTEGER z = { 0 }; ULONG got = 0;
        if (FAILED(IStream_Stat(st, &ss, STATFLAG_NONAME)) || ss.cbSize.QuadPart == 0 || ss.cbSize.QuadPart > 0x7FFFFFFF) goto done;
        *outSize = (DWORD)ss.cbSize.QuadPart; *outData = (BYTE *)malloc(*outSize);
        if (!*outData) goto done;
        st->lpVtbl->Seek(st, z, STREAM_SEEK_SET, NULL);
        if (FAILED(st->lpVtbl->Read(st, *outData, *outSize, &got)) || got != *outSize) { free(*outData); *outData = NULL; goto done; }
        ok = TRUE;
    }
done:
    REL(pal); REL(pb); REL(fr); REL(enc); REL(st); free(px8);
    return ok;
}

/* ---------------------------------------------------------------- printing */
static void PrintSheet(HDC pdc, const BYTE *cells, int x0, int y0, int Wp, int Hp, double cp)
{
    int stride = ((Wp + 31) / 32) * 4;
    BYTE *bits = (BYTE *)calloc((size_t)stride * Hp, 1);
    int *cx = (int *)malloc(Wp * sizeof(int));
    if (!bits || !cx) { free(bits); free(cx); return; }
    for (int x = 0; x < Wp; x++) cx[x] = MINW((int)(x / cp), g_cols - 1);
    for (int y = 0; y < Hp; y++) {
        const BYTE *row = cells + MINW((int)(y / cp), g_rows - 1) * g_cols;
        BYTE *o = bits + (size_t)y * stride;     /* top-down DIB, 1 = white */
        for (int x = 0; x < Wp; x++) if (!row[cx[x]]) o[x >> 3] |= (BYTE)(0x80 >> (x & 7));
    }
    struct { BITMAPINFOHEADER h; RGBQUAD pal[2]; } bi = { { sizeof(BITMAPINFOHEADER), Wp, -Hp, 1, 1, BI_RGB } };
    bi.pal[0] = (RGBQUAD){ 0, 0, 0, 0 }; bi.pal[1] = (RGBQUAD){ 255, 255, 255, 0 };
    SetStretchBltMode(pdc, COLORONCOLOR);
    StretchDIBits(pdc, x0, y0, Wp, Hp, 0, 0, Wp, Hp, bits, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    free(bits); free(cx);
}

static BOOL DoPrint(HWND hw)
{
    PRINTDLGW pd = { sizeof pd };
    pd.hwndOwner = hw; pd.nCopies = 1; pd.hDevMode = g_hDevMode; pd.hDevNames = g_hDevNames;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE;
    if (!PrintDlgW(&pd)) return TRUE;         /* cancelled is not an error */
    HDC pdc = pd.hDC; BOOL ok = FALSE;
    if (pdc) {
        int dpi = GetDeviceCaps(pdc, LOGPIXELSX), pw = GetDeviceCaps(pdc, HORZRES), ph = GetDeviceCaps(pdc, VERTRES);
        double margin = dpi * 10 / 25.4, gap = dpi * 12 / 25.4, N = MAXW(g_cols, g_rows);
        double cp = g_sizeCm / 2.54 * dpi / N;
        double Wf = g_cols * cp, Hf = g_rows * cp;
        double k = MINW(1.0, MINW((pw - 2 * margin) / Wf, (ph - 2 * margin - gap) / (2 * Hf)));
        cp *= k;
        int Wp = MAXW(1, (int)(g_cols * cp + 0.5)), Hp = MAXW(1, (int)(g_rows * cp + 0.5)), ig = (int)gap;
        int x0 = (pw - Wp) / 2, y0 = (ph - (2 * Hp + ig)) / 2;
        DOCINFOW di = { sizeof di, LoadStr(IDS_APPNAME) };
        if (StartDocW(pdc, &di) > 0) {
            if (StartPage(pdc) > 0) {
                PrintSheet(pdc, g_A, x0, y0, Wp, Hp, cp);
                PrintSheet(pdc, g_B, x0, y0 + Hp + ig, Wp, Hp, cp);
                HPEN p = CreatePen(PS_DASH, MAXW(1, dpi / 150), RGB(0, 0, 0)); HGDIOBJ o = SelectObject(pdc, p);
                int cy = y0 + Hp + ig / 2, ext = dpi / 5;
                MoveToEx(pdc, MAXW(0, x0 - ext), cy, NULL); LineTo(pdc, MINW(pw, x0 + Wp + ext), cy);
                SelectObject(pdc, o); DeleteObject(p);
                ok = EndPage(pdc) > 0;
            }
            if (ok) EndDoc(pdc); else AbortDoc(pdc);
        }
        DeleteDC(pdc);
    }
    g_hDevMode = pd.hDevMode; g_hDevNames = pd.hDevNames;     /* keep the user's printer choice */
    SavePrinter();
    return ok;
}

static void RefreshPaper(HWND hw)
{
    double wmm = 210, hmm = 297;
    WCHAR name[256] = L""; DWORD n = _countof(name);
    HDC d = NULL; DEVMODEW *dm = NULL; DEVNAMES *dn = NULL;
    if (g_hDevMode && g_hDevNames && (dm = (DEVMODEW *)GlobalLock(g_hDevMode)) && (dn = (DEVNAMES *)GlobalLock(g_hDevNames))) {
        wcsncpy_s(name, _countof(name), (const WCHAR *)dn + dn->wDeviceOffset, _TRUNCATE);
        d = CreateDCW(L"WINSPOOL", name, NULL, dm);
    }
    if (dn) GlobalUnlock(g_hDevNames);
    if (dm) GlobalUnlock(g_hDevMode);
    if (!d) {                                  /* nothing chosen (or it vanished): follow the Windows default printer */
        name[0] = 0;
        if (GetDefaultPrinterW(name, &n)) d = CreateDCW(L"WINSPOOL", name, NULL, NULL);
    }
    if (d) {
        int px = GetDeviceCaps(d, PHYSICALWIDTH), py = GetDeviceCaps(d, PHYSICALHEIGHT), dp = GetDeviceCaps(d, LOGPIXELSX);
        if (px > 0 && py > 0 && dp > 0) { wmm = px * 25.4 / dp; hmm = py * 25.4 / dp; }
        DeleteDC(d);
    }
    double side = MINW(wmm - 20.0, (hmm - 20.0 - 12.0) / 2.0) / 10.0;   /* cm for one sheet: 10 mm margins, 12 mm gap */
    int mx = CLAMPV((int)side, 4, 40);
    WCHAR t[300];
    swprintf_s(t, _countof(t), LoadStr(IDS_PRINTER_FMT), d ? name : LoadStr(IDS_PRINTER_NONE), (int)(wmm + 0.5), (int)(hmm + 0.5));
    if (wcscmp(t, g_prnText) != 0) {
        wcscpy_s(g_prnText, _countof(g_prnText), t);
        if (g_btn[9]) SetWindowTextW(g_btn[9], t);
    }
    if (mx != g_maxCm || g_sizeCm > mx) {
        g_maxCm = mx; g_sizeCm = MINW(g_sizeCm, g_maxCm);
        if (g_have && !g_dirty) { g_dirty = TRUE; SetStatus(LoadStr(IDS_ST_DIRTY)); }
        if (hw) InvalidateRect(hw, &rcCard[0], FALSE);
    }
    if (g_btn[9]) InvalidateRect(g_btn[9], NULL, FALSE);
}

static void PrinterSetup(HWND hw)
{
    PRINTDLGW pd = { sizeof pd };
    pd.hwndOwner = hw; pd.hDevMode = g_hDevMode; pd.hDevNames = g_hDevNames;
    pd.Flags = PD_PRINTSETUP;                  /* printer picker + properties + paper size + orientation */
    if (PrintDlgW(&pd)) { g_hDevMode = pd.hDevMode; g_hDevNames = pd.hDevNames; SavePrinter(); }
    RefreshPaper(hw);
}

/* ---------------------------------------------------------------- actions */
static void OpenImagePath(HWND hw, const WCHAR *path)
{
    if (!LoadImageFile(path)) { Msg(hw, LoadStr(IDS_ERR_OPEN), MB_OK | MB_ICONWARNING); return; }
    wcscpy_s(g_path, MAX_PATH, path);
    g_have = FALSE; g_dirty = FALSE; KillTimer(hw, TIMER_ANIM);
    BuildPreview(); UpdateButtons();
    SetStatusFmt(IDS_ST_OPENED, PathFindFileNameW(path), g_iw, g_ih);
    InvalidateRect(hw, NULL, FALSE);
}

static void DoMagic(HWND hw)
{
    if (!g_pix) return;
    Generate();
    g_t = 1.0; StageGeom();
    g_animStart = GetTickCount(); SetTimer(hw, TIMER_ANIM, 15, NULL);
    UpdateButtons();
    SetStatusFmt(IDS_ST_DONE, g_cols, g_rows);
    InvalidateRect(hw, NULL, FALSE);
}

static void MarkDirty(HWND hw)
{
    if (g_have && !g_dirty) { g_dirty = TRUE; SetStatus(LoadStr(IDS_ST_DIRTY)); }
    InvalidateRect(hw, &rcCard[0], FALSE);
}

static void ApplyTheme(HWND hw, BOOL dark)
{
    SetTheme(dark); ApplyTitleBar(hw); SetBtnText(); BuildPreview();
    for (int i = 0; i < 10; i++) InvalidateRect(g_btn[i], NULL, TRUE);
    InvalidateRect(hw, NULL, FALSE);
}

static void OnCommand(HWND hw, int id)
{
    switch (id) {
    case IDC_OPEN: { WCHAR p[MAX_PATH]; if (OpenDialog(hw, p)) OpenImagePath(hw, p); break; }
    case IDC_MAGIC: DoMagic(hw); break;
    case IDC_PRINTER: PrinterSetup(hw); break;
    case IDC_THEME: g_themePref = g_dark ? 0 : 1; RegSetInt(L"Theme", g_themePref); ApplyTheme(hw, g_themePref == 1); break;
    case IDC_SAVE: {
        WCHAR p[MAX_PATH]; int fmt = FMT_PNG;
        if (g_have && SaveDialog(hw, p, &fmt)) {
            if (SaveImage(p, fmt)) SetStatusFmt(IDS_ST_SAVED, p);
            else Msg(hw, LoadStr(IDS_ERR_SAVE), MB_OK | MB_ICONWARNING);
        }
        break; }
    case IDC_PRINT:
        if (g_have) {
            BOOL pok = DoPrint(hw); RefreshPaper(hw);
            if (pok) SetStatus(LoadStr(IDS_ST_PRINTED));
            else Msg(hw, LoadStr(IDS_ERR_PRINT), MB_OK | MB_ICONWARNING);
        }
        break;
    case IDC_SZ_DN: if (g_sizeCm > 4)        { g_sizeCm--; MarkDirty(hw); } break;
    case IDC_SZ_UP: if (g_sizeCm < g_maxCm)  { g_sizeCm++; MarkDirty(hw); } break;
    case IDC_DT_DN: if (g_detail > 48)       { g_detail -= 16; MarkDirty(hw); } break;
    case IDC_DT_UP: if (g_detail < 384)      { g_detail += 16; MarkDirty(hw); } break;
    }
}


/* ---------------------------------------------------------------- centered message box + window placement */
static HHOOK g_hook;
static LRESULT CALLBACK CbtProc(int code, WPARAM w, LPARAM l)
{
    if (code == HCBT_ACTIVATE) {
        HWND d = (HWND)w; RECT pr, dr;
        GetWindowRect(g_hwnd, &pr); GetWindowRect(d, &dr);
        MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        int x = pr.left + (RW(&pr) - RW(&dr)) / 2, y = pr.top + (RH(&pr) - RH(&dr)) / 2;
        x = CLAMPV(x, mi.rcWork.left, MAXW(mi.rcWork.left, mi.rcWork.right - RW(&dr)));
        y = CLAMPV(y, mi.rcWork.top, MAXW(mi.rcWork.top, mi.rcWork.bottom - RH(&dr)));
        SetWindowPos(d, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UnhookWindowsHookEx(g_hook); g_hook = NULL;
    }
    return CallNextHookEx(g_hook, code, w, l);
}

static int Msg(HWND hw, const WCHAR *text, UINT flags)
{
    g_hook = SetWindowsHookExW(WH_CBT, CbtProc, NULL, GetCurrentThreadId());
    int r = MessageBoxW(hw, text, LoadStr(IDS_APPNAME), flags);
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
    return r;
}

#define REGKEY L"Software\\Rekow IT\\Hidden Pictures"
static BOOL RegGetInt(const WCHAR *name, int *out)
{
    DWORD v, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, name, RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS) return FALSE;
    *out = (int)v; return TRUE;
}
static void RegSetInt(const WCHAR *name, int v)
{
    DWORD d = (DWORD)v; RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, name, REG_DWORD, &d, sizeof d);
}

/* The printer picked in the app (name + DEVMODE incl. paper/orientation) survives restarts. */
static void SavePrinter(void)
{
    if (!g_hDevMode || !g_hDevNames) return;
    DEVMODEW *dm = (DEVMODEW *)GlobalLock(g_hDevMode); DEVNAMES *dn = (DEVNAMES *)GlobalLock(g_hDevNames);
    if (dm && dn) {
        const WCHAR *name = (const WCHAR *)dn + dn->wDeviceOffset;
        RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"PrinterName", REG_SZ, name, (DWORD)((wcslen(name) + 1) * sizeof(WCHAR)));
        RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"PrinterDevMode", REG_BINARY, dm, dm->dmSize + dm->dmDriverExtra);
    }
    if (dn) GlobalUnlock(g_hDevNames);
    if (dm) GlobalUnlock(g_hDevMode);
}

static void LoadPrinter(void)
{
    WCHAR name[256]; DWORD nsz = sizeof name, dsz = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"PrinterName", RRF_RT_REG_SZ, NULL, name, &nsz) != ERROR_SUCCESS) return;
    if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"PrinterDevMode", RRF_RT_REG_BINARY, NULL, NULL, &dsz) != ERROR_SUCCESS || dsz < sizeof(DEVMODEW)) return;
    DEVMODEW *in = (DEVMODEW *)malloc(dsz), *out = NULL; HANDLE hp = NULL; HGLOBAL hm = NULL, hn = NULL;
    if (!in || RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"PrinterDevMode", RRF_RT_REG_BINARY, NULL, in, &dsz) != ERROR_SUCCESS) goto done;
    if ((DWORD)(in->dmSize + in->dmDriverExtra) > dsz) goto done;
    if (!OpenPrinterW(name, &hp, NULL)) goto done;                      /* printer no longer installed */
    LONG need = DocumentPropertiesW(NULL, hp, name, NULL, NULL, 0);     /* let the driver validate/merge the saved settings */
    if (need <= 0 || !(out = (DEVMODEW *)malloc(need))) goto done;
    if (DocumentPropertiesW(NULL, hp, name, out, in, DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK) goto done;
    hm = GlobalAlloc(GMEM_MOVEABLE, need);
    size_t drv = wcslen(L"winspool") + 1, dev = wcslen(name) + 1;
    hn = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVNAMES) + (drv + dev + 1) * sizeof(WCHAR));
    if (hm && hn) {
        void *p = GlobalLock(hm); memcpy(p, out, need); GlobalUnlock(hm);
        DEVNAMES *dn = (DEVNAMES *)GlobalLock(hn); WCHAR *base = (WCHAR *)dn;
        dn->wDriverOffset = sizeof(DEVNAMES) / sizeof(WCHAR);
        dn->wDeviceOffset = (WORD)(dn->wDriverOffset + drv);
        dn->wOutputOffset = (WORD)(dn->wDeviceOffset + dev);
        wcscpy_s(base + dn->wDriverOffset, drv, L"winspool"); wcscpy_s(base + dn->wDeviceOffset, dev, name);
        GlobalUnlock(hn);
        g_hDevMode = hm; g_hDevNames = hn; hm = hn = NULL;
    }
done:
    if (hm) GlobalFree(hm);
    if (hn) GlobalFree(hn);
    if (hp) ClosePrinter(hp);
    free(in); free(out);
}

static void SavePlacement(HWND hw)
{
    WINDOWPLACEMENT wp = { sizeof wp };
    if (!GetWindowPlacement(hw, &wp)) return;
    RECT *r = &wp.rcNormalPosition;
    RegSetInt(L"Left", r->left); RegSetInt(L"Top", r->top);
    RegSetInt(L"Width", MulDiv(RW(r), 96, g_dpi)); RegSetInt(L"Height", MulDiv(RH(r), 96, g_dpi));   /* stored at 96 dpi */
    RegSetInt(L"Maximized", wp.showCmd == SW_SHOWMAXIMIZED);
}

static void RestorePlacement(HWND hw, int show)
{
    int l = 0, t = 0, w = 0, h = 0, mx = 0; RECT r = { 0 };
    BOOL have = RegGetInt(L"Left", &l) && RegGetInt(L"Top", &t) && RegGetInt(L"Width", &w) && RegGetInt(L"Height", &h);
    RegGetInt(L"Maximized", &mx);
    if (have) {
        r = MkR(l, t, l + MulDiv(w, g_dpi, 96), t + MulDiv(h, g_dpi, 96));
        if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) have = FALSE;   /* monitor gone: fall back to centered */
    }
    if (!have) {
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int ww = MINW(S(1360), RW(&wa)), wh = MINW(S(840), RH(&wa));
        r = MkR(wa.left + (RW(&wa) - ww) / 2, wa.top + (RH(&wa) - wh) / 2, 0, 0);
        r.right = r.left + ww; r.bottom = r.top + wh;
        mx = 0;
    }
    WINDOWPLACEMENT wp = { sizeof wp };
    wp.rcNormalPosition = r; wp.showCmd = mx ? SW_SHOWMAXIMIZED : show;
    SetWindowPlacement(hw, &wp);
}

/* ---------------------------------------------------------------- window proc */
static BOOL InSheet(int k, int x, int y)
{
    RECT r = SheetRect(k); POINT p = { x, y }; return PtInRect(&r, p);
}

static void MoveSheetTo(HWND hw, int mx, int my)
{
    int lo_x = -(g_ox / g_cs), hi_x = (RW(&rcStageA) - g_ox) / g_cs - g_cols;
    int lo_y = -(g_oy / g_cs), hi_y = (RH(&rcStageA) - g_oy) / g_cs - g_rows;
    int cx = (int)floor((double)(mx - g_grab.x - rcStageA.left - g_ox) / g_cs + 0.5);
    int cy = (int)floor((double)(my - g_grab.y - rcStageA.top - g_oy) / g_cs + 0.5);
    cx = CLAMPV(cx, lo_x, hi_x); cy = CLAMPV(cy, lo_y, hi_y);
    if (g_dragSheet) { g_bx = cx; g_by = cy; } else { g_ax = cx; g_ay = cy; }
    double dx = g_bx - g_ax, dy = g_by - g_ay, dist = g_dist;
    g_t = CLAMPV(sqrt(dx * dx + dy * dy) / dist, 0.0, 1.0);
    InvalidateRect(hw, &rcStageA, FALSE); InvalidateRect(hw, &rcSlider, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hw, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE: {
        g_hwnd = hw;
        const int ids[10] = { IDC_OPEN, IDC_MAGIC, IDC_SAVE, IDC_PRINT, IDC_SZ_DN, IDC_SZ_UP, IDC_DT_DN, IDC_DT_UP, IDC_THEME, IDC_PRINTER };
        for (int i = 0; i < 10; i++) {
            g_btn[i] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hw, (HMENU)(INT_PTR)ids[i], g_inst, NULL);
            SetWindowSubclass(g_btn[i], BtnProc, 0, 0);
        }
        SetBtnText(); DragAcceptFiles(hw, TRUE);
        SetStatus(LoadStr(IDS_ST_READY));
        return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE && g_btn[9]) RefreshPaper(hw); break;
    case WM_PAINT: OnPaint(hw); return 0;
    case WM_SIZE: if (w != SIZE_MINIMIZED) { Layout(hw); InvalidateRect(hw, NULL, FALSE); } return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mi = (MINMAXINFO *)l; mi->ptMinTrackSize.x = S(1180); mi->ptMinTrackSize.y = S(620); return 0; }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w); RECT *r = (RECT *)l;
        SetWindowPos(hw, NULL, r->left, r->top, RW(r), RH(r), SWP_NOZORDER | SWP_NOACTIVATE);
        MakeFonts(); Layout(hw); InvalidateRect(hw, NULL, TRUE); return 0; }
    case WM_SETTINGCHANGE:
        if (l && g_themePref < 0 && lstrcmpW((LPCWSTR)l, L"ImmersiveColorSet") == 0) ApplyTheme(hw, SystemUsesDark());
        return 0;
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED) OnCommand(hw, LOWORD(w));
        return 0;
    case WM_DRAWITEM: DrawButton((const DRAWITEMSTRUCT *)l); return TRUE;
    case WM_DROPFILES: {
        WCHAR p[MAX_PATH]; HDROP d = (HDROP)w;
        if (DragQueryFileW(d, 0, p, MAX_PATH)) OpenImagePath(hw, p);
        DragFinish(d); return 0; }
    case WM_TIMER:
        if (w == TIMER_ANIM) {
            double p = (GetTickCount() - g_animStart) / 650.0;
            if (p >= 1.0) { p = 1.0; KillTimer(hw, TIMER_ANIM); }
            double e = 1.0 - pow(1.0 - p, 3.0);
            g_t = 1.0 - e; ApplyT();
            InvalidateRect(hw, &rcStageA, FALSE); InvalidateRect(hw, &rcSlider, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l); POINT pt = { x, y };
        SetFocus(hw); KillTimer(hw, TIMER_ANIM);
        if (PtInRect(&rcAbout, pt)) {
            Msg(hw, LoadStr(IDS_ABOUT_TEXT), MB_OK | MB_ICONINFORMATION); return 0;
        }
        if (g_have && PtInRect(&rcSlider, pt)) { g_drag = DRAG_SLIDER; SetCapture(hw); SetTFromX(hw, x); return 0; }
        if (g_have && PtInRect(&rcStageA, pt)) {
            int k = InSheet(1, x, y) ? 1 : InSheet(0, x, y) ? 0 : -1;
            if (k >= 0) {
                RECT r = SheetRect(k); g_dragSheet = k; g_grab.x = x - r.left; g_grab.y = y - r.top;
                g_drag = DRAG_SHEET; SetCapture(hw);
            }
            return 0;
        }
        int hit = g_pix ? HitSel(x, y) : 0;
        if (!hit && PtInRect(&rcSrc, pt)) hit = HNEW;      /* empty area: click opens a file, drag selects */
        if (hit) {
            g_pendClick = (hit == HNEW);
            g_drag = DRAG_SEL; g_dragMode = hit; g_dragPt.x = x; g_dragPt.y = y;
            g_oSX = g_sx; g_oSY = g_sy; g_oSW = g_sw; g_oSH = g_sh; SetCapture(hw);
        }
        return 0; }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
        if (g_drag == DRAG_SEL) {
            if (g_pendClick && abs(x - g_dragPt.x) <= S(4) && abs(y - g_dragPt.y) <= S(4)) return 0;
            g_pendClick = FALSE;
            if (g_pix) DragSel(hw, x, y);
        }
        else if (g_drag == DRAG_SHEET) MoveSheetTo(hw, x, y);
        else if (g_drag == DRAG_SLIDER) SetTFromX(hw, x);
        return 0; }
    case WM_LBUTTONUP:
        if (g_drag) {
            if (g_drag == DRAG_SHEET && abs(g_bx - g_ax) <= 1 && abs(g_by - g_ay) <= 1) {   /* magnetic snap */
                if (g_dragSheet) { g_bx = g_ax; g_by = g_ay; } else { g_ax = g_bx; g_ay = g_by; }
                g_t = 0.0; InvalidateRect(hw, &rcStageA, FALSE); InvalidateRect(hw, &rcSlider, FALSE);
            }
            int was = g_drag; BOOL click = g_pendClick;
            g_drag = DRAG_NONE; g_pendClick = FALSE; ReleaseCapture();
            if (was == DRAG_SEL && click) { g_dragMode = 0; PostMessageW(hw, WM_COMMAND, IDC_OPEN, 0); }
            else if (g_dragMode) { g_dragMode = 0; MarkDirty(hw); }
        }
        return 0;
    case WM_KEYDOWN:
        if (g_have) {
            int dx = (w == VK_RIGHT) - (w == VK_LEFT), dy = (w == VK_DOWN) - (w == VK_UP);
            if (dx || dy) { g_bx += dx; g_by += dy; g_t = CLAMPV(hypot(g_bx - g_ax, g_by - g_ay) / g_dist, 0.0, 1.0);
                            InvalidateRect(hw, &rcStageA, FALSE); InvalidateRect(hw, &rcSlider, FALSE); }
            else if (w == VK_HOME) { g_t = 0; ApplyT(); InvalidateRect(hw, &rcStageA, FALSE); InvalidateRect(hw, &rcSlider, FALSE); }
            else if (w == VK_END)  { g_t = 1; ApplyT(); InvalidateRect(hw, &rcStageA, FALSE); InvalidateRect(hw, &rcSlider, FALSE); }
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hw, &pt);
            LPCWSTR cur = IDC_ARROW;
            if (g_drag == DRAG_SHEET) cur = IDC_SIZEALL;
            else if (PtInRect(&rcAbout, pt)) cur = IDC_HAND;
            else if (g_have && PtInRect(&rcSlider, pt)) cur = IDC_HAND;
            else if (g_have && PtInRect(&rcStageA, pt)) cur = (InSheet(1, pt.x, pt.y) || InSheet(0, pt.x, pt.y)) ? IDC_SIZEALL : IDC_ARROW;
            else {
                int h = g_drag == DRAG_SEL ? g_dragMode : g_pix ? HitSel(pt.x, pt.y) : 0;
                if (!h && PtInRect(&rcSrc, pt)) cur = IDC_HAND;
                else if (h == HNEW) cur = IDC_CROSS; else if (h == HMOVE) cur = IDC_SIZEALL;
                else if (h == (HL | HT) || h == (HR | HB)) cur = IDC_SIZENWSE;
                else if (h == (HR | HT) || h == (HL | HB)) cur = IDC_SIZENESW;
                else if (h & (HL | HR)) cur = IDC_SIZEWE; else if (h & (HT | HB)) cur = IDC_SIZENS;
            }
            SetCursor(LoadCursorW(NULL, cur)); return TRUE;
        }
        break;
    case WM_CLOSE: SavePlacement(hw); DestroyWindow(hw); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hw, m, w, l);
}

/* ---------------------------------------------------------------- entry */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show)
{
    (void)hp; (void)cmd;
    g_inst = hi;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_STANDARD_CLASSES }; InitCommonControlsEx(&ic);
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&g_wic))) {
        MessageBoxW(NULL, L"WIC is not available.", L"Hidden Pictures", MB_OK | MB_ICONERROR); return 1;
    }
    LANGID ui = GetUserDefaultUILanguage();
    g_lang = PRIMARYLANGID(ui) == LANG_GERMAN ? MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN) : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    LARGE_INTEGER qc; QueryPerformanceCounter(&qc);
    g_rng = (unsigned long long)qc.QuadPart ^ ((unsigned long long)GetTickCount() << 32) ^ 0x9E3779B97F4A7C15ULL;
    if (!g_rng) g_rng = 1;

    { int tp; if (RegGetInt(L"Theme", &tp) && (tp == 0 || tp == 1)) g_themePref = tp; }
    SetTheme(g_themePref >= 0 ? g_themePref == 1 : SystemUsesDark());

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APP)); wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"HiddenPicturesWnd";
    RegisterClassExW(&wc);

    HDC sd = GetDC(NULL); g_dpi = GetDeviceCaps(sd, LOGPIXELSX); ReleaseDC(NULL, sd);
    MakeFonts();
    int ww = S(1360), wh = S(840);
    HWND hw = CreateWindowExW(0, wc.lpszClassName, LoadStr(IDS_APPNAME), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, ww, wh, NULL, NULL, hi, NULL);
    if (!hw) return 1;
    g_dpi = GetDpiForWindow(hw); MakeFonts();
    ApplyTitleBar(hw);
    LoadPrinter(); RefreshPaper(hw);
    int argc; WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    UpdateButtons();
    RestorePlacement(hw, show); UpdateWindow(hw);
    Layout(hw);
    if (argv && argc > 1) OpenImagePath(hw, argv[1]);
    if (argv) LocalFree(argv);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hw, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    FreeImage(); free(g_A); free(g_B); REL(g_wic);
    CoUninitialize();
    return (int)msg.wParam;
}
