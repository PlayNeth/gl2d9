// See lfb.h. The memory a lock gives is gl2d9's own, the same for reading and writing (as
// the Voodoo's one address range): a read lock fills it with the buffer as 16-bit pixels, a
// write lock with the buffer in the write mode's format and keeps a copy; at the write
// unlock the pixels that differ from the copy are the ones written. A write lock in a 32-bit
// mode has memory of its own (its rows are laid out differently: shared, a read lock's 16-bit
// fill wrote over the game's 32-bit pixels, and the other way round).
#include "lfb.h"

#include <algorithm>

#include "pixconv.h"
#include "render.h"

namespace gl2d9 {

namespace {

// The frame buffer rows a lock gives (gc->fbStride): the Voodoo's 1024 pixels, and 2048 for
// the wider modes of Glide 3 (1280 and up), whose rows 1024 can't hold: written at a 2048-byte
// stride, each 1600-pixel row ran into the next one (NFS Porsche Unleashed's loading screens
// at 1600x1200 came out in pieces).
int StridePixels() { return g_board.screenW > 1024 ? 2048 : 1024; }

struct LfbState {
    FxU8* mem = nullptr;  // StridePixels() * 2 bytes a row: reads, and 16-bit writes
    size_t size = 0;
    FxU8* mem32 = nullptr;  // StridePixels() * 4 bytes a row: 32-bit writes
    size_t size32 = 0;
    int readBuffer = -1, writeBuffer = -1;  // locked buffers (-1: none)
    GrOriginLocation_t readOrigin = GR_ORIGIN_UPPER_LEFT, writeOrigin = GR_ORIGIN_UPPER_LEFT;
    GrLfbWriteMode_t writeMode = GR_LFBWRITEMODE_565;
    bool pipeline = false;
    std::vector<FxU8> snapshot;  // the write lock's memory as filled
    std::vector<FxU32> pixels;   // scratch: a buffer read back, or pixels to write
    std::vector<FxU16> aux;      // and the aux buffer's
    std::vector<FxU8> mask;
};
LfbState g_l;

// Bytes a pixel of a write mode takes in the lock's memory.
int PixelBytes(int mode) {
    switch (mode) {
    case GR_LFBWRITEMODE_888:
    case GR_LFBWRITEMODE_8888:
    case GR_LFBWRITEMODE_565_DEPTH:
    case GR_LFBWRITEMODE_555_DEPTH:
    case GR_LFBWRITEMODE_1555_DEPTH: return 4;
    default: return 2;
    }
}

// Memory of `bytes`-byte pixels (2 or 4) for the frame, kept from lock to lock (zeroed when
// made: the columns past the screen's width stay 0, nothing writes them).
FxU8* Memory(int bytes) {
    FxU8*& mem = bytes == 4 ? g_l.mem32 : g_l.mem;
    size_t& size = bytes == 4 ? g_l.size32 : g_l.size;
    const size_t need = size_t(StridePixels()) * bytes * size_t(std::max(g_board.screenH, 1) + 1);
    if (mem && size >= need) return mem;
    if (mem) VirtualFree(mem, 0, MEM_RELEASE);
    mem = static_cast<FxU8*>(VirtualAlloc(nullptr, need, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    size = mem ? need : 0;
    return mem;
}
// The write lock's memory.
FxU8* WriteMem() { return PixelBytes(g_l.writeMode) == 4 ? g_l.mem32 : g_l.mem; }

int Lanes() { return int((St().r.lfbMode & SST_LFB_RGBALANES) >> SST_LFB_RGBALANES_SHIFT); }

// ARGB from a pixel of a write format (where the format has no alpha, grLfbConstantAlpha's:
// what the pixel pipeline tests and blends with).
FxU32 ToArgb(int mode, FxU32 v, int lanes) {
    FxU32 r, g, b, a = St().lfbConstantAlpha & 0xFF;
    switch (mode) {
    case GR_LFBWRITEMODE_565:
    case GR_LFBWRITEMODE_565_DEPTH:
        r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        r = (r << 3) | (r >> 2), g = (g << 2) | (g >> 4), b = (b << 3) | (b >> 2);
        break;
    case GR_LFBWRITEMODE_555:
    case GR_LFBWRITEMODE_555_DEPTH:
    case GR_LFBWRITEMODE_1555:
    case GR_LFBWRITEMODE_1555_DEPTH:
        r = (v >> 10) & 0x1F, g = (v >> 5) & 0x1F, b = v & 0x1F;
        r = (r << 3) | (r >> 2), g = (g << 3) | (g >> 2), b = (b << 3) | (b >> 2);
        if (mode == GR_LFBWRITEMODE_1555 || mode == GR_LFBWRITEMODE_1555_DEPTH) a = (v & 0x8000) ? 0xFF : 0;
        break;
    case GR_LFBWRITEMODE_888:
    case GR_LFBWRITEMODE_8888:
        // The lanes of grLfbWriteColorFormat: ARGB, ABGR, RGBA, BGRA.
        switch (lanes) {
        case 1: a = v >> 24, b = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, r = v & 0xFF; break;
        case 2: r = v >> 24, g = (v >> 16) & 0xFF, b = (v >> 8) & 0xFF, a = v & 0xFF; break;
        case 3: b = v >> 24, g = (v >> 16) & 0xFF, r = (v >> 8) & 0xFF, a = v & 0xFF; break;
        default: a = v >> 24, r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF; break;
        }
        if (mode == GR_LFBWRITEMODE_888) a = St().lfbConstantAlpha & 0xFF;
        return a << 24 | r << 16 | g << 8 | b;
    default: return 0;
    }
    // 16-bit lanes: ABGR and BGRA swap red and blue.
    if (lanes == 1 || lanes == 3) std::swap(r, b);
    return a << 24 | r << 16 | g << 8 | b;
}

// A pixel of a write format from ARGB (as the write lock's memory is filled).
FxU32 FromArgb(int mode, FxU32 c, int lanes) {
    FxU32 a = c >> 24, r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    switch (mode) {
    case GR_LFBWRITEMODE_888:
    case GR_LFBWRITEMODE_8888:
        switch (lanes) {
        case 1: return a << 24 | b << 16 | g << 8 | r;
        case 2: return r << 24 | g << 16 | b << 8 | a;
        case 3: return b << 24 | g << 16 | r << 8 | a;
        default: return a << 24 | r << 16 | g << 8 | b;
        }
    default: break;
    }
    if (lanes == 1 || lanes == 3) std::swap(r, b);
    switch (mode) {
    case GR_LFBWRITEMODE_555:
    case GR_LFBWRITEMODE_555_DEPTH: return (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
    case GR_LFBWRITEMODE_1555:
    case GR_LFBWRITEMODE_1555_DEPTH: return 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
    default: return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
    }
}

bool IsColorBuffer(int buffer) { return buffer == GR_BUFFER_FRONTBUFFER || buffer == GR_BUFFER_BACKBUFFER; }
// The write modes with a depth value in the upper 16 bits.
bool HasDepth(int mode) { return mode >= GR_LFBWRITEMODE_565_DEPTH && mode <= GR_LFBWRITEMODE_1555_DEPTH; }

// The pixel i of a row of `bytes`-byte pixels (2 or 4).
inline FxU32 Pixel(const FxU8* row, int i, int bytes) {
    if (bytes == 2) {
        FxU16 v;
        memcpy(&v, row + size_t(i) * 2, 2);
        return v;
    }
    FxU32 v;
    memcpy(&v, row + size_t(i) * 4, 4);
    return v;
}

// The write lock's changes to its buffer.
void Commit() {
    FxU8* const mem = WriteMem();
    if (g_l.writeBuffer < 0 || !mem || g_l.snapshot.empty()) return;
    const int W = g_board.screenW, H = g_board.screenH;
    const int bytes = PixelBytes(g_l.writeMode);
    const size_t stride = size_t(StridePixels()) * bytes;
    // The rectangle of changed pixels.
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++) {
        const FxU8* a = mem + size_t(y) * stride;
        const FxU8* b = g_l.snapshot.data() + size_t(y) * stride;
        if (memcmp(a, b, size_t(W) * bytes) == 0) continue;
        int first = 0, last = W - 1;
        while (Pixel(a, first, bytes) == Pixel(b, first, bytes)) first++;
        while (Pixel(a, last, bytes) == Pixel(b, last, bytes)) last--;
        x0 = std::min(x0, first), x1 = std::max(x1, last);
        y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
    GL_TRACE("lfb commit: buffer %d, changed %d,%d-%d,%d", g_l.writeBuffer, x0, y0, x1, y1);
    if (x1 < 0) return;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    const bool color = IsColorBuffer(g_l.writeBuffer);
    const bool depth = !color || HasDepth(g_l.writeMode);
    g_l.pixels.assign(color ? size_t(w) * h : 0, 0);
    g_l.aux.assign(depth ? size_t(w) * h : 0, 0);
    g_l.mask.assign(size_t(w) * h, 0);
    const int lanes = Lanes();
    const bool flip = g_l.writeOrigin == GR_ORIGIN_LOWER_LEFT;
    for (int j = 0; j < h; j++) {
        const int y = y0 + j;
        const FxU8* a = mem + size_t(y) * stride + size_t(x0) * bytes;
        const FxU8* b = g_l.snapshot.data() + size_t(y) * stride + size_t(x0) * bytes;
        // A lower-left origin: the memory's first row is the screen's bottom one.
        const int row = flip ? h - 1 - j : j;
        for (int i = 0; i < w; i++) {
            FxU32 v = Pixel(a, i, bytes);
            if (v == Pixel(b, i, bytes)) continue;
            const size_t k = size_t(row) * w + i;
            if (!color) {
                g_l.aux[k] = FxU16(v);
            } else {
                if (HasDepth(g_l.writeMode)) g_l.aux[k] = FxU16(v >> 16);
                if (bytes == 4 && g_l.writeMode != GR_LFBWRITEMODE_888 && g_l.writeMode != GR_LFBWRITEMODE_8888)
                    v &= 0xFFFF;
                g_l.pixels[k] = ToArgb(g_l.writeMode, v, lanes);
            }
            g_l.mask[k] = 1;
        }
    }
    if (g_l.pipeline) {  // (stipple rotate mode: the register turns for each pixel written)
        unsigned n = 0;
        for (FxU8 m : g_l.mask) n += m;
        RenderStippleAdvance(n);
    }
    const int sy = flip ? H - 1 - y1 : y0;
    RenderWritePixels(g_l.writeBuffer, x0, sy, w, h, color ? g_l.pixels.data() : nullptr,
                      depth ? g_l.aux.data() : nullptr, g_l.mask.data(), g_l.pipeline);
    // Written now: compared from here on with what the memory holds (else a later commit, at
    // a read, a region call, the swap or the unlock, wrote the same pixels again, over what
    // was drawn meanwhile, and blended or stippled them twice).
    for (int y = y0; y <= y1; y++)
        memcpy(g_l.snapshot.data() + size_t(y) * stride + size_t(x0) * bytes,
               mem + size_t(y) * stride + size_t(x0) * bytes, size_t(w) * bytes);
}

// The write lock's memory filled from its buffer, and the copy it is compared with.
void FillForWrite() {
    const int W = g_board.screenW, H = g_board.screenH;
    const int bytes = PixelBytes(g_l.writeMode);
    const size_t stride = size_t(StridePixels()) * bytes;
    FxU8* const mem = WriteMem();
    const bool flip = g_l.writeOrigin == GR_ORIGIN_LOWER_LEFT;
    const bool color = IsColorBuffer(g_l.writeBuffer);
    const bool haveColor = color && RenderReadBuffer(g_l.writeBuffer, &g_l.pixels);
    const bool haveAux = (!color || HasDepth(g_l.writeMode)) && RenderReadAux(&g_l.aux);
    const int lanes = Lanes();
    if (haveColor && !haveAux && g_l.writeMode == GR_LFBWRITEMODE_565 && (lanes == 0 || lanes == 2)) {
        // The common case, a row at a time. (Lanes 1 and 3 swap red and blue.)
        for (int y = 0; y < H; y++)
            pix::RowTo565(g_l.pixels.data() + size_t(flip ? H - 1 - y : y) * W,
                          reinterpret_cast<FxU16*>(mem + size_t(y) * stride), size_t(W));
        g_l.snapshot.assign(mem, mem + stride * H);
        return;
    }
    for (int y = 0; y < H; y++) {
        const size_t srcRow = size_t(flip ? H - 1 - y : y) * W;
        FxU8* row = mem + size_t(y) * stride;
        for (int x = 0; x < W; x++) {
            FxU32 v = 0;
            if (haveColor) v = FromArgb(g_l.writeMode, g_l.pixels[srcRow + x], lanes);
            if (haveAux) v = color ? (v & 0xFFFF) | FxU32(g_l.aux[srcRow + x]) << 16 : g_l.aux[srcRow + x];
            memcpy(row + size_t(x) * bytes, &v, bytes);
        }
    }
    g_l.snapshot.assign(mem, mem + stride * H);
}

}  // namespace

FxBool LfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin,
               FxBool pixelPipeline, GrLfbInfo_t* info) {
    if (!g_board.open || !info || !RenderReady() || !Memory(2)) return FXFALSE;
    type &= ~GR_LFB_NOIDLE;
    origin = origin == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
    if (type == GR_LFB_READ_ONLY) {
        if (g_l.readBuffer >= 0) return FXFALSE;
        if (buffer != GR_BUFFER_FRONTBUFFER && buffer != GR_BUFFER_BACKBUFFER && buffer != GR_BUFFER_AUXBUFFER)
            return FXFALSE;
        // What a write lock changed so far is in the buffer first.
        Commit();
        const int W = g_board.screenW, H = g_board.screenH;
        const size_t stride = size_t(StridePixels()) * 2;
        FxU16* m = reinterpret_cast<FxU16*>(g_l.mem);
        if (IsColorBuffer(buffer) && RenderReadBuffer(buffer, &g_l.pixels)) {
            for (int y = 0; y < H; y++) {
                const FxU32* src = g_l.pixels.data() + size_t(origin == GR_ORIGIN_LOWER_LEFT ? H - 1 - y : y) * W;
                pix::RowTo565(src, m + size_t(y) * StridePixels(), size_t(W));
            }
        } else if (!IsColorBuffer(buffer) && RenderReadAux(&g_l.aux)) {
            for (int y = 0; y < H; y++)
                memcpy(m + size_t(y) * StridePixels(),
                       g_l.aux.data() + size_t(origin == GR_ORIGIN_LOWER_LEFT ? H - 1 - y : y) * W, size_t(W) * 2);
        }
        else
            for (int y = 0; y < H; y++) memset(m + size_t(y) * StridePixels(), 0, size_t(W) * 2);
        if (g_l.writeBuffer >= 0 && PixelBytes(g_l.writeMode) == 2) {
            // Both locked, sharing the memory: the read fill becomes what the write lock
            // compares with.
            g_l.snapshot.assign(g_l.mem, g_l.mem + stride * H);
        }
        g_l.readBuffer = buffer;
        g_l.readOrigin = origin;
        info->strideInBytes = StridePixels() * 2;
        info->origin = origin;
        info->lfbPtr = g_l.mem;
        info->writeMode = GR_LFBWRITEMODE_565;
        St().r.lfbMode = (St().r.lfbMode & ~SST_LFB_YORIGIN) | (origin ? SST_LFB_YORIGIN : 0);
        return FXTRUE;
    }
    if (type != GR_LFB_WRITE_ONLY || g_l.writeBuffer >= 0) return FXFALSE;
    if (PixelBytes(writeMode == GR_LFBWRITEMODE_ANY ? GR_LFBWRITEMODE_565 : writeMode) == 4 && !Memory(4)) return FXFALSE;
    if (writeMode == GR_LFBWRITEMODE_ANY)
        writeMode = buffer == GR_BUFFER_AUXBUFFER ? GR_LFBWRITEMODE_ZA16 : GR_LFBWRITEMODE_565;
    switch (writeMode) {
    case GR_LFBWRITEMODE_565: case GR_LFBWRITEMODE_555: case GR_LFBWRITEMODE_1555: case GR_LFBWRITEMODE_888:
    case GR_LFBWRITEMODE_8888: case GR_LFBWRITEMODE_565_DEPTH: case GR_LFBWRITEMODE_555_DEPTH:
    case GR_LFBWRITEMODE_1555_DEPTH: case GR_LFBWRITEMODE_ZA16: break;
    default: return FXFALSE;
    }
    if (IsColorBuffer(buffer) ? writeMode == GR_LFBWRITEMODE_ZA16
                              : (buffer != GR_BUFFER_AUXBUFFER || writeMode != GR_LFBWRITEMODE_ZA16))
        return FXFALSE;
    g_l.writeBuffer = buffer;
    g_l.writeMode = writeMode;
    g_l.writeOrigin = origin;
    g_l.pipeline = pixelPipeline != FXFALSE;
    try {
        if (g_l.readBuffer >= 0 && PixelBytes(writeMode) == 2) {
            // Read-locked too: the memory already holds the 16-bit pixels the game may be
            // changing in place (as one address range). Compared with what it holds now.
            const size_t n = size_t(StridePixels()) * 2 * g_board.screenH;
            g_l.snapshot.assign(g_l.mem, g_l.mem + n);
        } else {
            FillForWrite();
        }
    } catch (...) {
        g_l.writeBuffer = -1;  // (not locked: the call fails, CallFailed, and the next can lock)
        throw;
    }
    FxU32 m = St().r.lfbMode & ~(SST_LFB_FORMAT | SST_LFB_YORIGIN | SST_LFB_ENPIXPIPE);
    m |= FxU32(writeMode) << SST_LFB_FORMAT_SHIFT;
    if (origin) m |= SST_LFB_YORIGIN;
    if (pixelPipeline) m |= SST_LFB_ENPIXPIPE;
    St().r.lfbMode = m;
    info->strideInBytes = FxU32(StridePixels() * PixelBytes(writeMode));
    info->origin = origin;
    info->lfbPtr = WriteMem();
    info->writeMode = writeMode;
    return FXTRUE;
}

FxBool LfbUnlock(GrLock_t type, GrBuffer_t buffer) {
    type &= ~GR_LFB_NOIDLE;
    if (type == GR_LFB_READ_ONLY) {
        if (g_l.readBuffer < 0 || g_l.readBuffer != buffer) return FXFALSE;
        g_l.readBuffer = -1;
        return FXTRUE;
    }
    if (type != GR_LFB_WRITE_ONLY || g_l.writeBuffer < 0 || g_l.writeBuffer != buffer) return FXFALSE;
    // Unlocked whatever happens: a commit that fails (memory run out, CallFailed) loses this
    // lock's writes, but left locked, every later write lock was refused (the game unlocks once).
    struct Unlock {
        ~Unlock() {
            g_l.writeBuffer = -1;
            g_l.snapshot.clear();
        }
    } unlock;
    Commit();
    return FXTRUE;
}

void LfbBeforeSwap() {
    if (g_l.writeBuffer < 0) return;
    // Still locked over the swap: its changes shown, and compared from here on with what
    // the memory holds now (the buffers moved: the memory still holds the old one's).
    Commit();  // (which brings the copy up to date)
}

void LfbReset() {
    g_l.readBuffer = g_l.writeBuffer = -1;
    g_l.snapshot.clear();
}

void LfbWriteColorFormat(GrColorFormat_t format) {
    St().r.lfbMode = (St().r.lfbMode & ~SST_LFB_RGBALANES) | ((FxU32(format) & 3) << SST_LFB_RGBALANES_SHIFT);
}

FxBool LfbReadRegion(GrBuffer_t src, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 dstStride, void* dst) {
    if (!g_board.open || !dst || !RenderReady()) return FXFALSE;
    const FxU32 W = FxU32(g_board.screenW), H = FxU32(g_board.screenH);
    if (x >= W || y >= H) return FXFALSE;
    w = std::min(w, W - x);
    h = std::min(h, H - y);
    Commit();
    const bool color = IsColorBuffer(src);
    if (color ? !RenderReadBuffer(src, &g_l.pixels) : !RenderReadAux(&g_l.aux)) return FXFALSE;
    for (FxU32 j = 0; j < h; j++) {
        FxU16* row = reinterpret_cast<FxU16*>(static_cast<FxU8*>(dst) + size_t(j) * dstStride);
        if (!color) {
            memcpy(row, g_l.aux.data() + size_t(y + j) * W + x, w * 2);
            continue;
        }
        pix::RowTo565(g_l.pixels.data() + size_t(y + j) * W + x, row, w);
    }
    return FXTRUE;
}

FxBool LfbWriteRegion(GrBuffer_t dst, FxU32 x, FxU32 y, GrLfbSrcFmt_t format, FxU32 w, FxU32 h, FxBool pixelPipeline,
                      FxI32 srcStride, const void* src) {
    if (!g_board.open || !src || !RenderReady()) return FXFALSE;
    const bool color = IsColorBuffer(dst);
    if (!color && dst != GR_BUFFER_AUXBUFFER) return FXFALSE;
    if (!color) format = GR_LFBWRITEMODE_ZA16;
    std::vector<FxU16> unpacked;
    if (format == GR_LFB_SRC_FMT_RLE16) {
        // guEncodeRLE16's runs (count << 16 | 565 color, the image as one row), unpacked to
        // 565. (3dfx's own grLfbWriteRegion fails these: "needs to be implemented".)
        if (!color || w > 4096 || h > 4096) return FXFALSE;  // (w * h as given, before the clip)
        unpacked.assign(size_t(w) * h, 0);
        const FxU32* run = static_cast<const FxU32*>(src);
        for (size_t at = 0; at < unpacked.size(); run++) {
            const size_t n = std::min<size_t>(*run >> 16, unpacked.size() - at);
            if (!n) break;  // (a zero count: the data ends)
            std::fill_n(unpacked.begin() + ptrdiff_t(at), n, FxU16(*run));
            at += n;
        }
        src = unpacked.data();
        srcStride = FxI32(w * 2);
        format = GR_LFBWRITEMODE_565;
    }
    // The source formats a colour buffer takes (3dfx's refuses the others: written, they
    // came out black).
    if (color) {
        switch (format) {
        case GR_LFBWRITEMODE_565: case GR_LFBWRITEMODE_555: case GR_LFBWRITEMODE_1555: case GR_LFBWRITEMODE_888:
        case GR_LFBWRITEMODE_8888: case GR_LFBWRITEMODE_565_DEPTH: case GR_LFBWRITEMODE_555_DEPTH:
        case GR_LFBWRITEMODE_1555_DEPTH: break;
        default: return FXFALSE;
        }
    }
    const FxU32 W = FxU32(g_board.screenW), H = FxU32(g_board.screenH);
    if (x >= W || y >= H) return FXTRUE;
    const FxU32 cw = std::min(w, W - x), ch = std::min(h, H - y);
    const int bytes = PixelBytes(int(format));
    const int lanes = Lanes();
    const bool depth = !color || HasDepth(int(format));
    Commit();
    g_l.pixels.assign(color ? size_t(cw) * ch : 0, 0);
    g_l.aux.assign(depth ? size_t(cw) * ch : 0, 0);
    for (FxU32 j = 0; j < ch; j++) {
        const FxU8* row = static_cast<const FxU8*>(src) + ptrdiff_t(j) * srcStride;
        for (FxU32 i = 0; i < cw; i++) {
            FxU32 v = 0;
            memcpy(&v, row + size_t(i) * bytes, bytes);
            const size_t k = size_t(j) * cw + i;
            if (!color) {
                g_l.aux[k] = FxU16(v);
                continue;
            }
            if (depth) g_l.aux[k] = FxU16(v >> 16);
            if (bytes == 4 && format != GR_LFBWRITEMODE_888 && format != GR_LFBWRITEMODE_8888) v &= 0xFFFF;
            g_l.pixels[k] = ToArgb(int(format), v, lanes);
        }
    }
    RenderWritePixels(dst, int(x), int(y), int(cw), int(ch), color ? g_l.pixels.data() : nullptr,
                      depth ? g_l.aux.data() : nullptr, nullptr, pixelPipeline != FXFALSE);
    return FXTRUE;
}

}  // namespace gl2d9
