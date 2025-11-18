#ifndef USE_COMPUTE_SKINNING
#define USE_COMPUTE_SKINNING 0
#endif

#ifndef USE_VERTEX_SKINNING
#define USE_VERTEX_SKINNING 0
#endif

// b0: ModelBuffer (VS) - ModelBufferType과 정확히 일치 (128 bytes)
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix; // 64 bytes
    row_major float4x4 WorldInverseTranspose; // 64 bytes - 올바른 노멀 변환을 위함
};

// b1: ViewProjBuffer (VS) - ViewProjBufferType과 일치
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;   // 0행 광원의 월드 좌표 + 스포트라이트 반경
    row_major float4x4 InverseProjectionMatrix; 
};

#if USE_COMPUTE_SKINNING
struct FSkinnedVertexInput
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
    float4 Tangent;
    float4 Color;
};
StructuredBuffer<FSkinnedVertexInput> g_SkinnedVertices : register(t12);
#elif USE_VERTEX_SKINNING
StructuredBuffer<float4x4> g_SkinnedMatrices : register(t12);
#endif

// --- 셰이더 입출력 구조체 ---
struct VS_INPUT
{
#if USE_COMPUTE_SKINNING    
    // Compute Shader Skinning은 입력이 없다
#elif USE_VERTEX_SKINNING
    float3 Position : POSITION;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT0;
    float4 Color : COLOR;
    uint4 BoneIndices : BLENDINDICES0;
    float4 BoneWeights : BLENDWEIGHT0;
#else
    float3 Position : POSITION;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT0;
    float4 Color : COLOR;
#endif    
};

// 출력은 오직 클립 공간 위치만 필요
struct VS_OUT
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
};

#if USE_VERTEX_SKINNING
float3 SkinPosition(float3 Position, uint4 BoneIndices, float4 BoneWeights)
{
    float4 SkinnedPos = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        if (BoneWeights[i] > 0.0f)
        {
            float4x4 BoneMatrix = g_SkinnedMatrices[BoneIndices[i]];
            SkinnedPos += mul(float4(Position, 1.0f), BoneMatrix) * BoneWeights[i];
        }
    }
    return SkinnedPos.xyz;
}
#endif

VS_OUT mainVS(VS_INPUT Input, uint VertexID : SV_VertexID)
{
    VS_OUT Output = (VS_OUT) 0;

    float3 SkinnedPos;

#if USE_COMPUTE_SKINNING
    SkinnedPos = g_SkinnedVertices[VertexID].Position;
#elif USE_VERTEX_SKINNING
    SKinnedPos = SkinPosition(Input.Position, Input.BoneIndices, Input.BoneWeights);
#else
    SkinnedPos = Input.Position;
#endif    
    
    // 모델 좌표 -> 월드 좌표 -> 뷰 좌표 -> 클립 좌표
    float4 WorldPos = mul(float4(SkinnedPos, 1.0f), WorldMatrix);
    float4 ViewPos = mul(WorldPos, ViewMatrix);
    Output.Position = mul(ViewPos, ProjectionMatrix);
    Output.WorldPosition = WorldPos.xyz;
    
    return Output;
}