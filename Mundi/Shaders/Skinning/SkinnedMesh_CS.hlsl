struct CS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT0;
    float4 Color : COLOR;
    uint4 BoneIndices : BLENDINDICES0;
    float4 BoneWeights : BLENDWEIGHT0;
};

struct CS_OUTPUT // FNormalVertex
{
    float3 Position : POSITION;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT0;
    float4 Color : COLOR;
};

cbuffer VertexCountBuffer : register(b0)
{
    uint VertexCount;
    uint3 Padding;
}

StructuredBuffer<CS_INPUT> g_Vertices : register(t0);
StructuredBuffer<float4x4> g_SkinnedMatrices : register(t1);
StructuredBuffer<float4x4> g_SkinnedNormalMatrices : register(t2);

// 스키닝된 정점, 노말, 탄젠트
RWStructuredBuffer<CS_OUTPUT> g_OutputVertices : register(u0);

float3 SkinPosition(float3 Position, uint4 BoneIndices, float4 BoneWeights)
{
    float4 SkinnedPos = float4(0, 0, 0, 0);
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        if (BoneWeights[i] > 0.0f)
        {
            float4x4 BoneMatrix = g_SkinnedMatrices[BoneIndices[i]];
            SkinnedPos += mul(BoneMatrix, float4(Position, 1.0f)) * BoneWeights[i];
        }
    }

    return SkinnedPos.xyz;
}

float3 SkinVector(float3 Vector, uint4 BoneIndices, float4 BoneWeights, StructuredBuffer<float4x4> MatrixBuffer)
{
    float3 SkinnedVector = 0.0f;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        if (BoneWeights[i] > 0.0f)
        {
            float4x4 BoneMatrix4x4 = MatrixBuffer[BoneIndices[i]];
            float3x3 BoneMatrix = (float3x3)BoneMatrix4x4;
            SkinnedVector += mul(BoneMatrix, Vector) * BoneWeights[i];
        }
    }

    return normalize(SkinnedVector);
}

[numthreads(64, 1, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{
    uint VertexIndex = DTid.x;

    if (VertexIndex >= VertexCount)
    {
        return;
    }

    CS_INPUT Input = g_Vertices[VertexIndex];
    float3 SkinnedPosition = float4(0, 0, 0, 0);
    float3 SkinnedNormal = float3(0, 0, 0);
    float3 SkinnedTangent = float3(0, 0, 0);
    
    SkinnedPosition = SkinPosition(Input.Position, Input.BoneIndices, Input.BoneWeights);
    SkinnedNormal = SkinVector(Input.Normal, Input.BoneIndices, Input.BoneWeights, g_SkinnedNormalMatrices);
    SkinnedTangent = SkinVector(Input.Tangent, Input.BoneIndices, Input.BoneWeights, g_SkinnedMatrices);


    CS_OUTPUT Output = (CS_OUTPUT)0;
    Output.Position = SkinnedPosition;
    Output.Normal = SkinnedNormal;
    Output.Tangent = float4(SkinnedTangent, Input.Tangent.w);
    Output.Color = Input.Color;
    Output.TexCoord = Input.TexCoord;

    g_OutputVertices[VertexIndex] = Output;
}