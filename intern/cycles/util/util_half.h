/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __UTIL_HALF_H__
#define __UTIL_HALF_H__

#include "util_types.h"

#ifdef __KERNEL_SSE2__
#include "util_simd.h"
#endif

CCL_NAMESPACE_BEGIN

/* Half Floats */

/* CUDA and OpenCL have inbuilt half data type, declare for CPU */
#ifndef __KERNEL_GPU__
typedef unsigned short half;
#endif

/* OpenCL knows half4, declare for CPU and CUDA */
#ifndef __KERNEL_OPENCL__
struct half4 { half x, y, z, w; };
#endif

/* Float <-> Half conversion.
 * We define three main functions for each architecture.
 * Half data is always loaded / written via pointers.
 * float4_store_half()
 * half_to_float()
 * half_to_float4()
 */

/* OpenCL */
#if defined(__KERNEL_OPENCL__)
#  define float4_store_half(h, f, scale) vstore_half4(f * (scale), 0, h);
#  define half_to_float(h) vload_half(0, h);
#  define half4_to_float4(h) vload_half4(0, h);

/* CUDA */
#elif defined(__KERNEL_CUDA__)

ccl_device_inline void float4_store_half(half *h, float4 f, float scale)
{
	h[0] = __float2half(f.x * scale);
	h[1] = __float2half(f.y * scale);
	h[2] = __float2half(f.z * scale);
	h[3] = __float2half(f.w * scale);
}

ccl_device_inline float half_to_float(half h)
{
	return __half2float(h);
}

ccl_device_inline float4 half4_to_float4(half4 h)
{
	float4 f;

	f.x = half_to_float(h.x);
	f.y = half_to_float(h.y);
	f.z = half_to_float(h.z);
	f.w = half_to_float(h.w);

	return f;
}

/* CPU */
#else

ccl_device_inline void float4_store_half(half *h, float4 f, float scale)
{
#ifndef __KERNEL_SSE2__
	for(int i = 0; i < 4; i++) {
		/* optimized float to half for pixels:
		 * assumes no negative, no nan, no inf, and sets denormal to 0 */
		union { uint i; float f; } in;
		float fscale = f[i] * scale;
		in.f = (fscale > 0.0f)? ((fscale < 65504.0f)? fscale: 65504.0f): 0.0f;
		int x = in.i;

		int absolute = x & 0x7FFFFFFF;
		int Z = absolute + 0xC8000000;
		int result = (absolute < 0x38800000)? 0: Z;
		int rshift = (result >> 13);

		h[i] = (rshift & 0x7FFF);
	}
#else
	/* same as above with SSE */
	ssef fscale = load4f(f) * scale;
	ssef x = min(max(fscale, 0.0f), 65504.0f);

#ifdef __KERNEL_AVX2__
	ssei rpack = _mm_cvtps_ph(x, 0);
#else
	ssei absolute = cast(x) & 0x7FFFFFFF;
	ssei Z = absolute + 0xC8000000;
	ssei result = andnot(absolute < 0x38800000, Z);
	ssei rshift = (result >> 13) & 0x7FFF;
	ssei rpack = _mm_packs_epi32(rshift, rshift);
#endif

	_mm_storel_pi((__m64*)h, _mm_castsi128_ps(rpack));
#endif
}

ccl_device_inline float half_to_float(half h)
{
	float f;

	*((int*) &f) = ((h & 0x8000) << 16) | (((h & 0x7c00) + 0x1C000) << 13) | ((h & 0x03FF) << 13);

	return f;
}

ccl_device_inline float4 half4_to_float4(half4 h)
{
	float4 f;

	f.x = half_to_float(h.x);
	f.y = half_to_float(h.y);
	f.z = half_to_float(h.w);
	f.w = half_to_float(h.z);

	return f;
}

#endif

CCL_NAMESPACE_END

#endif /* __UTIL_HALF_H__ */
