#include <stdint.h>
#include <windows.h>
#include <avisynth.h>

class MSharpen : public GenericVideoFilter {
	double _threshold;
	double _strength;
	bool _mask;
	bool _luma, _chroma;
	bool processPlane[3];
	bool has_at_least_v8;

	template <typename T>
	void msharpenEdgeMask(PVideoFrame& mask, PVideoFrame& blur, PVideoFrame& src, IScriptEnvironment* env);
	template <typename T>
	void sharpen(PVideoFrame& dst, PVideoFrame& blur, PVideoFrame& src, IScriptEnvironment* env);

public:
	MSharpen(PClip _child, float threshold, float strength, bool mask, bool luma, bool chroma, IScriptEnvironment* env);
	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
	int __stdcall SetCacheHints(int cachehints, int frame_range)
	{
		return cachehints == CACHE_GET_MTMODE ? MT_MULTI_INSTANCE : 0;
	}
};

template <typename T>
static inline void blur3x3(void* maskpv, const void* srcpv, int stride, int width, int height) {
	T* maskp = (T*)maskpv;
	const T* srcp = (const T*)srcpv;

	stride /= sizeof(T);

	maskp[0] = (srcp[0] + srcp[1] +
		srcp[stride] + srcp[stride + 1]) / 4;

	for (int x = 1; x < width - 1; x++)
		maskp[x] = (srcp[x - 1] + srcp[x] + srcp[x + 1] +
			srcp[x + stride - 1] + srcp[x + stride] + srcp[x + stride + 1]) / 6;

	maskp[width - 1] = (srcp[width - 2] + srcp[width - 1] +
		srcp[width + stride - 2] + srcp[width + stride - 1]) / 4;

	srcp += stride;
	maskp += stride;

	for (int y = 1; y < height - 1; y++) {
		maskp[0] = (srcp[-stride] + srcp[-stride + 1] +
			srcp[0] + srcp[1] +
			srcp[stride] + srcp[stride + 1]) / 6;

		for (int x = 1; x < width - 1; x++)
			maskp[x] = (srcp[x - stride - 1] + srcp[x - stride] + srcp[x - stride + 1] +
				srcp[x - 1] + srcp[x] + srcp[x + 1] +
				srcp[x + stride - 1] + srcp[x + stride] + srcp[x + stride + 1]) / 9;

		maskp[width - 1] = (srcp[width - stride - 2] + srcp[width - stride - 1] +
			srcp[width - 2] + srcp[width - 1] +
			srcp[width + stride - 2] + srcp[width + stride - 1]) / 6;

		srcp += stride;
		maskp += stride;
	}

	maskp[0] = (srcp[-stride] + srcp[-stride + 1] +
		srcp[0] + srcp[1]) / 4;

	for (int x = 1; x < width - 1; x++)
		maskp[x] = (srcp[x - stride - 1] + srcp[x - stride] + srcp[x - stride + 1] +
			srcp[x - 1] + srcp[x] + srcp[x + 1]) / 6;

	maskp[width - 1] = (srcp[width - stride - 2] + srcp[width - stride - 1] +
		srcp[width - 2] + srcp[width - 1]) / 4;
}

static inline int clamp(int val, int minimum, int maximum) {
	if (val < minimum)
		val = minimum;
	else if (val > maximum)
		val = maximum;

	return val;
}

template <typename T>
static inline void msharpenFindEdges(void* maskpv, const void* srcpv, int stride, int width, int height, int th, int maximum) {
	T* maskp = (T*)maskpv;
	const T* srcp = (const T*)srcpv;

	stride /= sizeof(T);

	for (int y = 0; y < height - 1; y++) {
		for (int x = 0; x < width - 1; x++) {
			int edge = abs(srcp[x] - srcp[x + stride + 1]) >= th ||
				abs(srcp[x + 1] - srcp[x + stride]) >= th ||
				abs(srcp[x] - srcp[x + 1]) >= th ||
				abs(srcp[x] - srcp[x + stride]) >= th;

			if (edge)
				maskp[x] = maximum;
			else
				maskp[x] = 0;
		}

		int edge = abs(srcp[width - 1] - srcp[width + stride - 1]) >= th;

		if (edge)
			maskp[width - 1] = maximum;
		else
			maskp[width - 1] = 0;

		maskp += stride;
		srcp += stride;
	}

	for (int x = 0; x < width - 1; x++) {
		int edge = abs(srcp[x] - srcp[x + 1]) >= th;

		if (edge)
			maskp[x] = maximum;
		else
			maskp[x] = 0;
	}

	maskp[width - 1] = maximum;
}

template <typename T>
void MSharpen::msharpenEdgeMask(PVideoFrame& mask, PVideoFrame& blur, PVideoFrame& src, IScriptEnvironment* env) {
	const uint8_t* srcp[3];
	uint8_t* maskp[3];
	uint8_t* blurp[3];
	int width[3];
	int height[3];
	int stride[3];

	int maximum = 0xffff >> (16 - vi.BitsPerComponent());
	int threshold = (int)((_threshold * maximum) / 100);
	threshold = clamp(threshold, 0, maximum);

	int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
	int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
	const int* current_planes = (vi.IsYUV() || vi.IsYUVA()) ? planes_y : planes_r;
	int planecount = min(vi.NumComponents(), 3);
	for (int i = 0; i < planecount; i++)
	{
		const int plane = current_planes[i];

		if (!processPlane[i])
		{
			copy_plane(blur, src, plane, env);
			copy_plane(mask, src, plane, env);			
			continue;
		}

		stride[i] = src->GetPitch(plane);
		width[i] = src->GetRowSize(plane) / vi.ComponentSize();
		height[i] = src->GetHeight(plane);
		srcp[i] = src->GetReadPtr(plane);
		maskp[i] = mask->GetWritePtr(plane);
		blurp[i] = blur->GetWritePtr(plane);

		blur3x3<T>(blurp[i], srcp[i], stride[i], width[i], height[i]);

		msharpenFindEdges<T>(maskp[i], blurp[i], stride[i], width[i], height[i], threshold, maximum);
	}

	if (vi.IsPlanarRGB())
	{
		for (int x = 0; x < height[0] * stride[0]; x++)
			maskp[0][x] = maskp[0][x] | maskp[1][x] | maskp[2][x];

		memcpy(maskp[1], maskp[0], static_cast<int64_t>(height[0]) * stride[0]);
		memcpy(maskp[2], maskp[0], static_cast<__int64>(height[0]) * stride[0]);
	}
}


template <typename T>
void MSharpen::sharpen(PVideoFrame& dst, PVideoFrame& blur, PVideoFrame& src, IScriptEnvironment* env) {

	int maximum = 0xffff >> (16 - vi.BitsPerComponent());
	int strength = (int)((_strength * maximum) / 100);
	strength = clamp(strength, 0, maximum);

	int invstrength = maximum - strength;

	int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
	int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
	const int* current_planes = (vi.IsYUV() || vi.IsYUVA()) ? planes_y : planes_r;
	int planecount = min(vi.NumComponents(), 3);
	for (int i = 0; i < planecount; i++)
	{
		const int plane = current_planes[i];

		if (!processPlane[i])
		{
			copy_plane(dst, src, plane, env);
			continue;
		}

		int stride = src->GetPitch(plane);
		int width = src->GetRowSize(plane) / vi.ComponentSize();
		int height = src->GetHeight(plane);
		const T* srcp = (const T*)src->GetReadPtr(plane);
		const T* blurp = (const T*)blur->GetReadPtr(plane);
		T* dstp = (T*)dst->GetWritePtr(plane);

		stride /= sizeof(T);

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				if (dstp[x]) {
					int tmp = 4 * srcp[x] - 3 * blurp[x];
					tmp = clamp(tmp, 0, maximum);

					dstp[x] = (strength * tmp + invstrength * srcp[x]) >> vi.BitsPerComponent();
				}
				else {
					dstp[x] = srcp[x];
				}
			}

			srcp += stride;
			dstp += stride;
			blurp += stride;
		}
	}
}

static void copy_plane(PVideoFrame& dst, PVideoFrame& src, int plane, IScriptEnvironment* env) {
	const uint8_t* srcp = src->GetReadPtr(plane);
	int src_pitch = src->GetPitch(plane);
	int height = src->GetHeight(plane);
	int row_size = src->GetRowSize(plane);
	uint8_t* destp = dst->GetWritePtr(plane);
	int dst_pitch = dst->GetPitch(plane);
	env->BitBlt(destp, dst_pitch, srcp, src_pitch, row_size, height);
}

MSharpen::MSharpen(PClip _child, float threshold, float strength, bool mask, bool luma, bool chroma, IScriptEnvironment* env)
	: GenericVideoFilter(_child), _threshold(threshold), _strength(strength), _mask(mask), _luma(luma), _chroma(chroma)
{
	has_at_least_v8 = true;
	try { env->CheckVersion(8); } catch (const AvisynthError&) { has_at_least_v8 = false; }

	int planecount = min(vi.NumComponents(), 3);
	for (int i = 0; i < planecount; i++)
	{
		if (vi.IsPlanarRGB())
			processPlane[i] = true;
		else if (i == 0) // Y
			processPlane[i] = _luma;
		else
			processPlane[i] = _chroma;
	}

	if (!vi.IsPlanar())
	{
		env->ThrowError("MSharpen: Clip must be in planar format.");
	}

	if (vi.BitsPerComponent() == 32)
	{
		env->ThrowError("MSharpen: Only 8..16 bit integer input supported.");
	}

	if (threshold < 0.0 || threshold > 100.0)
	{
		env->ThrowError("MSharpen: threshold must be between 0..100.");
	}

	if (strength < 0.0 || strength > 100.0)
	{
		env->ThrowError("MSharpen: strength must be between 0..100.");
	}
}

PVideoFrame MSharpen::GetFrame(int n, IScriptEnvironment* env)
{
	PVideoFrame src = child->GetFrame(n, env);
	PVideoFrame blur = env->NewVideoFrame(vi);
	PVideoFrame dst;
	if (has_at_least_v8) dst = env->NewVideoFrameP(vi, &src); else dst = env->NewVideoFrame(vi);

	if (vi.BitsPerComponent() == 8)
	{
		msharpenEdgeMask<uint8_t>(dst, blur, src, env);
	}
	else
	{
		msharpenEdgeMask<uint16_t>(dst, blur, src, env);
	}

	if (!_mask)
	{
		if (vi.BitsPerComponent() == 8)
		{
			sharpen<uint8_t>(dst, blur, src, env);
		}
		else
		{
			sharpen<uint16_t>(dst, blur, src, env);
		}
	}

	return dst;
}

AVSValue __cdecl Create_MSharpen(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	return new MSharpen(
		args[0].AsClip(),
		args[1].AsFloat(6.0),
		args[2].AsFloat(39.0),
		args[3].AsBool(false),
		args[4].AsBool(true),
		args[5].AsBool(false),		
		env);
}

const AVS_Linkage* AVS_linkage;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
	AVS_linkage = vectors;

	env->AddFunction("vsMSharpen", "c[threshold]f[strength]f[mask]b[luma]b[chroma]b", Create_MSharpen, NULL);
	return "vsMSharpen";
}