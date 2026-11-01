// Highlight fade: a*(1-t) + b*t on linear RGBA16F pictures.

cbuffer Params : register(b0)
{
	float t;
	float3 pad;
};

Texture2D<float4> a : register(t0);
Texture2D<float4> b : register(t1);
RWTexture2D<float4> dst : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint w, h;
	dst.GetDimensions(w, h);
	if (id.x >= w || id.y >= h)
		return;
	dst[id.xy] = lerp(a.Load(int3(id.xy, 0)), b.Load(int3(id.xy, 0)), t);
}