Texture2D frameTexture : register(t0);
SamplerState frameSampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_TARGET
{
	return frameTexture.Sample(frameSampler, texcoord);
}
