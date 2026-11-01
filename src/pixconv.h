// Pixel row conversions: Voodoo texel and frame buffer formats to and from 32-bit ARGB
// (0xAARRGGBB), a row at a time, with SSE2 for the 16-bit RGB formats. Every channel is
// widened by repeating its top bits (5 bits: (v << 3) | (v >> 2)), as the hardware does.
// Free of Windows and of the rest of gl2d9, so tests can check them against Scalar*.
#pragma once
#include <emmintrin.h>

#include <cstddef>
#include <cstdint>

namespace gl2d9 {
namespace pix {

// ---------------------------------------------------------------- scalar (the reference)

inline uint32_t Scalar565(uint32_t v) {
    const uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return 0xFF000000u | ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
}
inline uint32_t Scalar1555(uint32_t v) {
    const uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    return (v & 0x8000 ? 0xFF000000u : 0) | ((r << 3) | (r >> 2)) << 16 | ((g << 3) | (g >> 2)) << 8 |
           ((b << 3) | (b >> 2));
}
inline uint32_t Scalar4444(uint32_t v) {
    return ((v >> 12) & 15) * 0x11000000u | ((v >> 8) & 15) * 0x110000u | ((v >> 4) & 15) * 0x1100u | (v & 15) * 0x11u;
}
inline uint16_t ScalarTo565(uint32_t c) {
    return uint16_t(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

// ---------------------------------------------------------------- rows

// 16-bit RGB texels to ARGB, eight at a time.
namespace detail {
// Two registers of eight 16-bit lanes (G << 8 | B, A << 8 | R) interleaved into eight ARGB pixels.
inline void Store8(uint32_t* out, __m128i gb, __m128i ar) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm_unpacklo_epi16(gb, ar));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 4), _mm_unpackhi_epi16(gb, ar));
}
inline __m128i Widen5(__m128i x) { return _mm_or_si128(_mm_slli_epi16(x, 3), _mm_srli_epi16(x, 2)); }
}  // namespace detail

inline void Row565(const uint16_t* in, uint32_t* out, size_t n) {
    size_t i = 0;
    const __m128i m6 = _mm_set1_epi16(63), m5 = _mm_set1_epi16(31), a = _mm_set1_epi16(short(0xFF00));
    for (; i + 8 <= n; i += 8) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i));
        const __m128i r = detail::Widen5(_mm_srli_epi16(v, 11));
        __m128i g = _mm_and_si128(_mm_srli_epi16(v, 5), m6);
        g = _mm_or_si128(_mm_slli_epi16(g, 2), _mm_srli_epi16(g, 4));
        const __m128i b = detail::Widen5(_mm_and_si128(v, m5));
        detail::Store8(out + i, _mm_or_si128(_mm_slli_epi16(g, 8), b), _mm_or_si128(a, r));
    }
    for (; i < n; i++) out[i] = Scalar565(in[i]);
}

inline void Row1555(const uint16_t* in, uint32_t* out, size_t n) {
    size_t i = 0;
    const __m128i m5 = _mm_set1_epi16(31), aMask = _mm_set1_epi16(short(0xFF00));
    for (; i + 8 <= n; i += 8) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i));
        const __m128i r = detail::Widen5(_mm_and_si128(_mm_srli_epi16(v, 10), m5));
        const __m128i g = detail::Widen5(_mm_and_si128(_mm_srli_epi16(v, 5), m5));
        const __m128i b = detail::Widen5(_mm_and_si128(v, m5));
        const __m128i a = _mm_and_si128(_mm_srai_epi16(v, 15), aMask);  // the top bit: 0xFF00 or 0
        detail::Store8(out + i, _mm_or_si128(_mm_slli_epi16(g, 8), b), _mm_or_si128(a, r));
    }
    for (; i < n; i++) out[i] = Scalar1555(in[i]);
}

inline void Row4444(const uint16_t* in, uint32_t* out, size_t n) {
    size_t i = 0;
    const __m128i m4 = _mm_set1_epi16(15);
    auto widen = [](__m128i x) { return _mm_or_si128(_mm_slli_epi16(x, 4), x); };
    for (; i + 8 <= n; i += 8) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i));
        const __m128i a = widen(_mm_srli_epi16(v, 12));
        const __m128i r = widen(_mm_and_si128(_mm_srli_epi16(v, 8), m4));
        const __m128i g = widen(_mm_and_si128(_mm_srli_epi16(v, 4), m4));
        const __m128i b = widen(_mm_and_si128(v, m4));
        detail::Store8(out + i, _mm_or_si128(_mm_slli_epi16(g, 8), b), _mm_or_si128(_mm_slli_epi16(a, 8), r));
    }
    for (; i < n; i++) out[i] = Scalar4444(in[i]);
}

// 8-bit texels through a 256-entry table of ARGB (the format's colors, a palette, NCC).
inline void Row8(const uint8_t* in, const uint32_t* lut, uint32_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = lut[in[i]];
}
// 16-bit texels: an 8-bit color through the table, alpha the high byte (8332, AP88, AYIQ, AI88).
inline void Row8A8(const uint16_t* in, const uint32_t* lut, uint32_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) out[i] = (lut[in[i] & 0xFF] & 0xFFFFFFu) | uint32_t(in[i] >> 8) << 24;
}

// ARGB to 565, eight at a time.
inline void RowTo565(const uint32_t* in, uint16_t* out, size_t n) {
    size_t i = 0;
    const __m128i mr = _mm_set1_epi32(0xF800), mg = _mm_set1_epi32(0x07E0), mb = _mm_set1_epi32(0x001F);
    auto four = [&](const uint32_t* p) {
        const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        __m128i v = _mm_or_si128(_mm_and_si128(_mm_srli_epi32(c, 8), mr), _mm_and_si128(_mm_srli_epi32(c, 5), mg));
        v = _mm_or_si128(v, _mm_and_si128(_mm_srli_epi32(c, 3), mb));
        return _mm_srai_epi32(_mm_slli_epi32(v, 16), 16);  // sign-extended: packs keeps the bits
    };
    for (; i + 8 <= n; i += 8)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), _mm_packs_epi32(four(in + i), four(in + i + 4)));
    for (; i < n; i++) out[i] = ScalarTo565(in[i]);
}

}  // namespace pix
}  // namespace gl2d9
