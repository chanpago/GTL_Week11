#include "pch.h"
#include "SkeletalMesh.h"


#include "FbxLoader.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"
#include "PathUtils.h"
#include <filesystem>

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::USkeletalMesh()
{
}

USkeletalMesh::~USkeletalMesh()
{
    ReleaseResources();
}

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice)
{
    if (Data)
    {
        ReleaseResources();
    }

    // FBXLoader가 캐싱을 내부적으로 처리합니다
    Data = UFbxLoader::GetInstance().LoadFbxMeshAsset(InFilePath);

    if (!Data || Data->Vertices.empty() || Data->Indices.empty())
    {
        UE_LOG("ERROR: Failed to load FBX mesh from '%s'", InFilePath.c_str());
        return;
    }

    // GPU 버퍼 생성
    CreateIndexBuffer(Data, InDevice);
    VertexCount = static_cast<uint32>(Data->Vertices.size());
    IndexCount = static_cast<uint32>(Data->Indices.size());
    CPUSkinnedVertexStride = sizeof(FVertexDynamic);
    GPUSkinnedVertexStride = sizeof(FSkinnedVertex);
}

void USkeletalMesh::ReleaseResources()
{
    if (IndexBuffer)
    {
        IndexBuffer->Release();
        IndexBuffer = nullptr;
    }

    if (Data)
    {
        delete Data;
        Data = nullptr;
    }
}

void USkeletalMesh::CreateCPUSkinnedVertexBuffer(ID3D11Buffer** InVertexBuffer)
{
    if (!Data) { return; }
    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    HRESULT hr = D3D11RHI::CreateVertexBuffer<FVertexDynamic>(Device, Data->Vertices, InVertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::CreateGPUSkinnedVertexBuffer(ID3D11Buffer** InVertexBuffer)
{
    if (!Data)
    {
        return;
    }
    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    HRESULT hr = D3D11RHI::CreateVertexBuffer<FSkinnedVertex>(Device, Data->Vertices, InVertexBuffer);
    assert(SUCCEEDED(hr));
}

void USkeletalMesh::UpdateVertexBuffer(const TArray<FNormalVertex>& SkinnedVertices, ID3D11Buffer* InVertexBuffer)
{
    if (!InVertexBuffer) { return; }

    GEngine.GetRHIDevice()->VertexBufferUpdate(InVertexBuffer, SkinnedVertices);
}

void USkeletalMesh::CreateStructuredBuffer(ID3D11Buffer** InStructuredBuffer, ID3D11ShaderResourceView** InShaderResourceView, UINT ElementCount)
{
    if (!InStructuredBuffer || !InShaderResourceView || !Data)
    {
        return;
    }

    D3D11RHI *RHI = GEngine.GetRHIDevice();
    HRESULT hr = RHI->CreateStructuredBuffer(sizeof(FMatrix), ElementCount, nullptr, InStructuredBuffer);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateStructuredBuffer] Structured buffer ceation fail");
        return;
    }

    hr = RHI->CreateStructuredBufferSRV(*InStructuredBuffer, InShaderResourceView);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateStructuredBuffer] Structured bufferSRV ceation fail");
        return;
    }    
}

void USkeletalMesh::CreateCSInputBuffer(ID3D11Buffer** InStructuredBuffer,
    ID3D11ShaderResourceView** InShaderResourceView, UINT InElementCount)
{
    D3D11_BUFFER_DESC bufferDesc = {};
    // IMMUTALBE 플래그로 생성과 동시에 정점 데이터 업로드
    // 절대 변경할 수 없으므로 Update 불필요
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.ByteWidth = sizeof(FSkinnedVertex) * InElementCount;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(FSkinnedVertex);

    D3D11_SUBRESOURCE_DATA InitData = {};
    InitData.pSysMem = Data->Vertices.GetData();
   
    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    HRESULT hr = Device->CreateBuffer(&bufferDesc, &InitData, InStructuredBuffer);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateCSInputBuffer] Structured buffer ceation fail");
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    SRVDesc.Buffer.NumElements = InElementCount;

    hr = Device->CreateShaderResourceView(*InStructuredBuffer, &SRVDesc, InShaderResourceView);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateCSInputBuffer] Structured bufferSRV ceation fail");
        return;
    }
}

void USkeletalMesh::CreateCSOutputBuffer(ID3D11Buffer** InStructuredBuffer,
    ID3D11UnorderedAccessView** InUnorderedAccessView, ID3D11ShaderResourceView** InShaderResourceView,
    UINT InElementCount)
{
    D3D11_BUFFER_DESC BufferDesc = {};
    BufferDesc.Usage = D3D11_USAGE_DEFAULT;
    BufferDesc.ByteWidth = sizeof(FNormalVertex) * InElementCount;
    BufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    BufferDesc.StructureByteStride = sizeof(FNormalVertex);

    ID3D11Device* Device = GEngine.GetRHIDevice()->GetDevice();
    HRESULT hr = Device->CreateBuffer(&BufferDesc, nullptr, InStructuredBuffer);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateCSOutputBuffer] Structured buffer ceation fail");
        return;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};    
    UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
    UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    UAVDesc.Buffer.NumElements = InElementCount;

    hr = Device->CreateUnorderedAccessView(*InStructuredBuffer, &UAVDesc, InUnorderedAccessView);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateCSOutputBuffer] Structured bufferUAV ceation fail");
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    SRVDesc.Buffer.NumElements = InElementCount;
    hr = Device->CreateShaderResourceView(*InStructuredBuffer, &SRVDesc, InShaderResourceView);
    if (FAILED(hr))
    {
        UE_LOG("[USkeletalMesh/CreateCSOutputBuffer] Structured bufferSRV ceation fail");
        return;
    }
}

void USkeletalMesh::BuildLocalAABBs()
{
    if (!Data || Data->Vertices.IsEmpty() || Data->Skeleton.Bones.IsEmpty())
    {
        return;
    }

    const uint32 BoneCount = GetBoneCount();
    const FAABB InValidAABB = FAABB(FVector(FLT_MAX), FVector(-FLT_MAX));
    // 이전 AABB 초기화
    BoneLocalAABBs.Empty();
    BoneLocalAABBs.SetNum(BoneCount, InValidAABB);
    for (const FSkinnedVertex& Vertex : Data->Vertices)
    {
        // 최대 4개의 본이 영향을 줌
        for (int32 i = 0; i < 4; i++)
        {
            const float Weight = Vertex.BoneWeights[i];
            // 가중치가 0보다 커야 정점에 영향을 줌
            // 영향을 받는 정점만 계산
            if (Weight > 0)
            {
                const uint32 BoneIndex = Vertex.BoneIndices[i];

                if (BoneIndex < BoneCount)
                {
                    // Min, Max가 같은 PointAABB
                    FAABB PointAABB(Vertex.Position, Vertex.Position);
                    // PointAABB와 기존 AABB를 Union 해나간다.
                    BoneLocalAABBs[BoneIndex] = FAABB::Union(BoneLocalAABBs[BoneIndex], PointAABB);
                }
            }
        }
    }
}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMeshData* InSkeletalMesh, ID3D11Device* InDevice)
{
    HRESULT hr = D3D11RHI::CreateIndexBuffer(InDevice, InSkeletalMesh, &IndexBuffer);
    assert(SUCCEEDED(hr));
}
