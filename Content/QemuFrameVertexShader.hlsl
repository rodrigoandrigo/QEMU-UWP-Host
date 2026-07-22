cbuffer FrameConstants : register(b0)
{
	float4 scaleOffset;
};

struct VertexInput
{
	float2 position : POSITION;
	float2 texcoord : TEXCOORD0;
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

VertexOutput main(VertexInput input)
{
	VertexOutput output;
	float2 scaled = input.position * scaleOffset.xy + scaleOffset.zw;
	output.position = float4(scaled, 0.0f, 1.0f);
	output.texcoord = input.texcoord;
	return output;
}
