#include "pch.h"
#include "SkinnedMeshComponent.h"

#include "Keyboard.h"
#include "MeshBatchElement.h"
#include "PlatformTime.h"
#include "SceneView.h"

USkinnedMeshComponent::USkinnedMeshComponent() : SkeletalMesh(nullptr)
{
   bCanEverTick = true;
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
   if (CPUSkinnedVertexBuffer)
   {
      CPUSkinnedVertexBuffer->Release();
      CPUSkinnedVertexBuffer = nullptr;
   }

   if (SkinningMatrixSRV)
   {
      SkinningMatrixSRV->Release();
      SkinningMatrixSRV = nullptr;
   }
   
   if (SkinningMatrixBuffer)
   {
      SkinningMatrixBuffer->Release();
      SkinningMatrixBuffer = nullptr;
   }

   if (SkinningNormalMatrixSRV)
   {
      SkinningNormalMatrixSRV->Release();
      SkinningNormalMatrixSRV = nullptr;
   }
   
   if (SkinningNormalMatrixBuffer)
   {
      SkinningNormalMatrixBuffer->Release();
      SkinningNormalMatrixBuffer = nullptr;
   }

   if (GPUSkinnedVertexBuffer)
   {
      GPUSkinnedVertexBuffer->Release();
      GPUSkinnedVertexBuffer = nullptr;
   }

   if (CSVertexBuffer)
   {
      CSVertexBuffer->Release();
      CSVertexBuffer = nullptr;
   }

   if (CSVertexSRV)
   {
      CSVertexSRV->Release();
      CSVertexSRV = nullptr;
   }

   if (CSOutputVertexUAV)
   {
      CSOutputVertexUAV->Release();
      CSOutputVertexUAV = nullptr;
   }

   if (CSOutVertexSRV)
   {
      CSOutVertexSRV->Release();
      CSOutVertexSRV = nullptr;
   }
   
   if (CSOutputBuffer)
   {
      CSOutputBuffer->Release();
      CSOutputBuffer = nullptr;
   }
}

void USkinnedMeshComponent::BeginPlay()
{
   Super::BeginPlay();
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
   UMeshComponent::TickComponent(DeltaTime);
}

void USkinnedMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
   Super::Serialize(bInIsLoading, InOutHandle);

   if (bInIsLoading)
   {
      SetSkeletalMesh(SkeletalMesh->GetPathFileName());
   }
   // @TODO - UStaticMeshComponent처럼 프로퍼티 기반 직렬화 로직 추가
}

void USkinnedMeshComponent::DuplicateSubObjects()
{
   Super::DuplicateSubObjects();
   
   InitializeGPUSkinningResource();
}


// void USkinnedMeshComponent::RenderDebugVolume(class URenderer* Renderer) const
// {
//    Super::RenderDebugVolume(Renderer);
//
//    FAABB WorldAABB = GetWorldAABB();
//    TArray<FVector> Vertices = WorldAABB.GetVertices();
//
//    FVector4 Color = FVector4(1.0f, 0.25f, 0.25f, 1.0f);
//    
//    Renderer->AddLine(Vertices[0], Vertices[4], Color);
//    Renderer->AddLine(Vertices[4], Vertices[7], Color);
//    Renderer->AddLine(Vertices[7], Vertices[3], Color);
//    Renderer->AddLine(Vertices[3], Vertices[0], Color);
//    
//    Renderer->AddLine(Vertices[4], Vertices[5], Color);
//    Renderer->AddLine(Vertices[5], Vertices[6], Color);
//    Renderer->AddLine(Vertices[6], Vertices[7], Color);
//    Renderer->AddLine(Vertices[7], Vertices[4], Color);
//    
//    Renderer->AddLine(Vertices[5], Vertices[6], Color);
//    Renderer->AddLine(Vertices[6], Vertices[2], Color);
//    Renderer->AddLine(Vertices[2], Vertices[1], Color);
//    Renderer->AddLine(Vertices[1], Vertices[5], Color);
//    
//    Renderer->AddLine(Vertices[0], Vertices[1], Color);
//    Renderer->AddLine(Vertices[1], Vertices[2], Color);
//    Renderer->AddLine(Vertices[2], Vertices[3], Color);
//    Renderer->AddLine(Vertices[3], Vertices[0], Color);
//    Renderer->EndLineBatch(FMatrix::Identity());
// }

void USkinnedMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
   if (!SkeletalMesh || !SkeletalMesh->GetSkeletalMeshData()) { return; }

   bComputeShaderSkinning = GetWorld()->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_ComputeSkinning);
   bVertexShaderSkinning = GetWorld()->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_VertexSkinning);

   if (bSkinningMatricesDirty && !bComputeShaderSkinning && !bVertexShaderSkinning)
   {
      TIME_PROFILE(VertexBuffer)
      bSkinningMatricesDirty = false;
      SkeletalMesh->UpdateVertexBuffer(SkinnedVertices, CPUSkinnedVertexBuffer);
      TIME_PROFILE_END(VertexBuffer)
   }

   if (bVertexShaderSkinning &&
      bIsResourcePrepare &&
      !FinalSkinningMatrices.IsEmpty())
   {
      TIME_PROFILE(StructuredBuffer)
      D3D11RHI* RHIDevice = GEngine.GetRHIDevice();
      RHIDevice->UpdateStructuredBuffer(SkinningMatrixBuffer,
                                        FinalSkinningMatrices.data(),
                                        sizeof(FMatrix) *
                                        FinalSkinningMatrices.Num());
   
      RHIDevice->UpdateStructuredBuffer(SkinningNormalMatrixBuffer,
                                        FinalSkinningNormalMatrices.data(),
                                        sizeof(FMatrix) *
                                        FinalSkinningNormalMatrices.Num());
      TIME_PROFILE_END(StructuredBuffer)
   }

   const TArray<FGroupInfo>& MeshGroupInfos = SkeletalMesh->GetMeshGroupInfo();
   auto DetermineMaterialAndShader = [&](uint32 SectionIndex) -> TPair<UMaterialInterface*, UShader*>
   {
      UMaterialInterface* Material = GetMaterial(SectionIndex);
      UShader* Shader = nullptr;

      if (Material && Material->GetShader())
      {
         Shader = Material->GetShader();
      }
      else
      {
         // UE_LOG("USkinnedMeshComponent: 머티리얼이 없거나 셰이더가 없어서 기본 머티리얼 사용 section %u.", SectionIndex);
         Material = UResourceManager::GetInstance().GetDefaultMaterial();
         if (Material)
         {
            Shader = Material->GetShader();
         }
         if (!Material || !Shader)
         {
            UE_LOG("USkinnedMeshComponent: 기본 머티리얼이 없습니다.");
            return {nullptr, nullptr};
         }
      }
      return {Material, Shader};
   };

   const bool bHasSections = !MeshGroupInfos.IsEmpty();
   const uint32 NumSectionsToProcess = bHasSections ? static_cast<uint32>(MeshGroupInfos.size()) : 1;

   for (uint32 SectionIndex = 0; SectionIndex < NumSectionsToProcess; ++SectionIndex)
   {
      uint32 IndexCount = 0;
      uint32 StartIndex = 0;

      if (bHasSections)
      {
         const FGroupInfo& Group = MeshGroupInfos[SectionIndex];
         IndexCount = Group.IndexCount;
         StartIndex = Group.StartIndex;
      }
      else
      {
         IndexCount = SkeletalMesh->GetIndexCount();
         StartIndex = 0;
      }

      if (IndexCount == 0)
      {
         continue;
      }

      auto [MaterialToUse, ShaderToUse] = DetermineMaterialAndShader(SectionIndex);
      if (!MaterialToUse || !ShaderToUse)
      {
         continue;
      }

      FMeshBatchElement BatchElement;
      TArray<FShaderMacro> ShaderMacros = View->ViewShaderMacros;
      if (bComputeShaderSkinning)
      {
         ShaderMacros.Add(FShaderMacro("USE_COMPUTE_SKINNING", "1"));
      }
      else if (bVertexShaderSkinning)
      {
         ShaderMacros.Add(FShaderMacro("USE_VERTEX_SKINNING", "1"));
      }

      if (0 < MaterialToUse->GetShaderMacros().Num())
      {
         ShaderMacros.Append(MaterialToUse->GetShaderMacros());
      }
      FShaderVariant* ShaderVariant = ShaderToUse->GetOrCompileShaderVariant(ShaderMacros);

      if (ShaderVariant)
      {
         BatchElement.VertexShader = ShaderVariant->VertexShader;
         BatchElement.PixelShader = ShaderVariant->PixelShader;
         BatchElement.InputLayout = ShaderVariant->InputLayout;
      }

      BatchElement.Material = MaterialToUse;

      if (bComputeShaderSkinning)
      {
         BatchElement.VertexBuffer = nullptr;
         BatchElement.VertexStride = 0;
         // CS의 계산 결과가 있는 SRV, VS의 입력
         BatchElement.ComputeShaderSkinMatrixSRV = CSOutVertexSRV;
      }
      else if (bVertexShaderSkinning)
      {         
         BatchElement.VertexBuffer = GPUSkinnedVertexBuffer;
         BatchElement.VertexStride = SkeletalMesh->GetGPUSkinnedVertexStride();
         BatchElement.VertexShaderSkinMatrixSRV = SkinningMatrixSRV;
         BatchElement.VertexShaderSkinNormalMatrixSRV = SkinningNormalMatrixSRV;
      }
      else
      {
         BatchElement.VertexBuffer = CPUSkinnedVertexBuffer;
         BatchElement.VertexStride = SkeletalMesh->GetCPUSkinnedVertexStride();
         BatchElement.ComputeShaderSkinMatrixSRV = nullptr;
         BatchElement.VertexShaderSkinMatrixSRV = nullptr;
         BatchElement.VertexShaderSkinNormalMatrixSRV = nullptr;
      }

      BatchElement.IndexBuffer = SkeletalMesh->GetIndexBuffer();
      BatchElement.IndexCount = IndexCount;
      BatchElement.StartIndex = StartIndex;
      BatchElement.BaseVertexIndex = 0;
      BatchElement.WorldMatrix = GetWorldMatrix();
      BatchElement.ObjectID = InternalIndex;
      BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

      OutMeshBatchElements.Add(BatchElement);
   }   
}

FAABB USkinnedMeshComponent::GetWorldAABB() const
{
   if (!SkeletalMesh)
   {
      return {};
   }

   const TArray<FAABB>& BoneLocalAABBs = SkeletalMesh->GetLocalAABBs();
   // 유효성 검사
   // BoneLocalAABB, Skinning matrix의 개수는 같아야함
   if (BoneLocalAABBs.Num() != FinalSkinningMatrices.Num())
   {
      return{};
   }
   
   FAABB WorldAABB = FAABB(FVector(FLT_MAX), FVector(-FLT_MAX));
   const uint32 BoneCount = SkeletalMesh->GetBoneCount();
   const FMatrix& WorldMatrix = GetWorldTransform().ToMatrix();
   for (int32 i = 0; i < BoneCount; i++)
   {
      const FAABB& LocalAABB = BoneLocalAABBs[i];
      // 유효하지 않은 AABB는 생략
      if (!LocalAABB.IsValid())
      {
         continue;
      }
   
      TArray<FVector> LocalCorners = LocalAABB.GetVertices();
      FMatrix CurrentSKinningMatrix = FinalSkinningMatrices[i];
      FMatrix BoneToWorld = CurrentSKinningMatrix * WorldMatrix;
      for (const FVector& Corner : LocalCorners)
      {
         // 로컬 꼭짓점 -> 월드 꼭짓점
         FVector WorldCorner = BoneToWorld.TransformPosition(Corner);
         FAABB PointAABB(WorldCorner, WorldCorner);
   
         WorldAABB = FAABB::Union(WorldAABB, PointAABB);
      }
   }

   return WorldAABB;
}

void USkinnedMeshComponent::OnTransformUpdated()
{
   Super::OnTransformUpdated();
   MarkWorldPartitionDirty();
}

void USkinnedMeshComponent::DispatchGPUSkinning(D3D11RHI* RHIDevice)
{
   if (!bComputeShaderSkinning || !bIsResourcePrepare || bVertexShaderSkinning)
   {
      return;
   }   

   auto SkinningShader = SkinningCS->GetComputeShader();
   if (!SkinningCS || !SkinningShader)
   {
      return;
   }   

   RHIDevice->UpdateStructuredBuffer(SkinningMatrixBuffer,
                                     FinalSkinningMatrices.data(),
                                     sizeof(FMatrix) *
                                     FinalSkinningMatrices.Num());
   
   RHIDevice->UpdateStructuredBuffer(SkinningNormalMatrixBuffer,
                                     FinalSkinningNormalMatrices.data(),
                                     sizeof(FMatrix) *
                                     FinalSkinningNormalMatrices.Num());


   ID3D11DeviceContext* DeviceContext = RHIDevice->GetDeviceContext();
   DeviceContext->CSSetShader(SkinningCS->GetComputeShader(), nullptr, 0);
   
   FSkinnedVertexCountBuffer VertexCountBuffer = { 0 };
   VertexCountBuffer.VertexCount = SkeletalMesh->GetVertexCount();
   RHIDevice->SetAndUpdateConstantBuffer(VertexCountBuffer);

   ID3D11ShaderResourceView* CSSRVs[] = { CSVertexSRV, SkinningMatrixSRV, SkinningNormalMatrixSRV };
   DeviceContext->CSSetShaderResources(0, 3, CSSRVs);

   ID3D11UnorderedAccessView* CSUAVs[] = { CSOutputVertexUAV };
   DeviceContext->CSSetUnorderedAccessViews(0, 1, CSUAVs, nullptr);

   const uint32 ThreadGroupSize = 64;
   const uint32 VertexCount = SkeletalMesh->GetVertexCount();
   const uint32 ThreadGroup = (VertexCount + ThreadGroupSize - 1) / ThreadGroupSize;

   DeviceContext->Dispatch(ThreadGroup, 1, 1);

   ID3D11ShaderResourceView* NullSRVs[] = { nullptr, nullptr, nullptr };
   DeviceContext->CSSetShaderResources(0, 3, NullSRVs);

   ID3D11UnorderedAccessView* NullUAVs[] = { nullptr };
   DeviceContext->CSSetUnorderedAccessViews(0, 1, NullUAVs, nullptr);
}

void USkinnedMeshComponent::SetSkeletalMesh(const FString& PathFileName)
{
   ClearDynamicMaterials();

   SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(PathFileName);

   if (CPUSkinnedVertexBuffer)
   {
      CPUSkinnedVertexBuffer->Release();
      CPUSkinnedVertexBuffer = nullptr;
   }

   if (SkinningMatrixSRV)
   {
      SkinningMatrixSRV->Release();
      SkinningMatrixSRV = nullptr;
   }
   
   if (SkinningMatrixBuffer)
   {
      SkinningMatrixBuffer->Release();
      SkinningMatrixBuffer = nullptr;
   }

   if (SkinningNormalMatrixSRV)
   {
      SkinningNormalMatrixSRV->Release();
      SkinningNormalMatrixSRV = nullptr;
   }
   
   if (SkinningNormalMatrixBuffer)
   {
      SkinningNormalMatrixBuffer->Release();
      SkinningNormalMatrixBuffer = nullptr;
   }

   if (GPUSkinnedVertexBuffer)
   {
      GPUSkinnedVertexBuffer->Release();
      GPUSkinnedVertexBuffer = nullptr;
   }

   if (CSVertexBuffer)
   {
      CSVertexBuffer->Release();
      CSVertexBuffer = nullptr;
   }

   if (CSVertexSRV)
   {
      CSVertexSRV->Release();
      CSVertexSRV = nullptr;
   }

   if (CSOutputVertexUAV)
   {
      CSOutputVertexUAV->Release();
      CSOutputVertexUAV = nullptr;
   }

   if (CSOutVertexSRV)
   {
      CSOutVertexSRV->Release();
      CSOutVertexSRV = nullptr;
   }
   
   if (CSOutputBuffer)
   {
      CSOutputBuffer->Release();
      CSOutputBuffer = nullptr;
   }
   
   if (SkeletalMesh && SkeletalMesh->GetSkeletalMeshData())
   {
      InitializeGPUSkinningResource();
      
      const TArray<FMatrix> IdentityMatrices(SkeletalMesh->GetBoneCount(), FMatrix::Identity());
      UpdateSkinningMatrices(IdentityMatrices, IdentityMatrices);
      PerformSkinning();
      
      const TArray<FGroupInfo>& GroupInfos = SkeletalMesh->GetMeshGroupInfo();
       MaterialSlots.resize(GroupInfos.size());
       for (int i = 0; i < GroupInfos.size(); ++i)
      {
         // FGroupInfo에 InitialMaterialName이 있다고 가정
         SetMaterialByName(i, GroupInfos[i].InitialMaterialName);
      }
      MarkWorldPartitionDirty();
      SkeletalMesh->BuildLocalAABBs();
   }
   else
   {
      SkeletalMesh = nullptr;
      UpdateSkinningMatrices(TArray<FMatrix>(), TArray<FMatrix>());
      PerformSkinning();
   }
}

void USkinnedMeshComponent::PerformSkinning()
{
   if (!SkeletalMesh || FinalSkinningMatrices.IsEmpty()) { return; }
   if (!bSkinningMatricesDirty) { return; }

   if (bComputeShaderSkinning || bVertexShaderSkinning)
   {      
      return;
   }

   TIME_PROFILE(CPUSkinning)
   const TArray<FSkinnedVertex>& SrcVertices = SkeletalMesh->GetSkeletalMeshData()->Vertices;
   const int32 NumVertices = SrcVertices.Num();
   SkinnedVertices.SetNum(NumVertices);

   for (int32 Idx = 0; Idx < NumVertices; ++Idx)
   {
      const FSkinnedVertex& SrcVert = SrcVertices[Idx];
      FNormalVertex& DstVert = SkinnedVertices[Idx];

      DstVert.pos = SkinVertexPosition(SrcVert); 
      DstVert.normal = SkinVertexNormal(SrcVert);
      DstVert.Tangent = SkinVertexTangent(SrcVert);
      DstVert.tex = SrcVert.UV;
   }
   TIME_PROFILE_END(CPUSkinning)   
}

void USkinnedMeshComponent::UpdateSkinningMatrices(const TArray<FMatrix>& InSkinningMatrices, const TArray<FMatrix>& InSkinningNormalMatrices)
{
   FinalSkinningMatrices = InSkinningMatrices;
   FinalSkinningNormalMatrices = InSkinningNormalMatrices;
   bSkinningMatricesDirty = true;   

}

FVector USkinnedMeshComponent::SkinVertexPosition(const FSkinnedVertex& InVertex) const
{
   FVector BlendedPosition(0.f, 0.f, 0.f);

   for (int32 Idx = 0; Idx < 4; ++Idx)
   {
      const uint32 BoneIndex = InVertex.BoneIndices[Idx];
      const float Weight = InVertex.BoneWeights[Idx];

      if (Weight > 0.f)
      {
         const FMatrix& SkinMatrix = FinalSkinningMatrices[BoneIndex];
         FVector TransformedPosition = SkinMatrix.TransformPosition(InVertex.Position);
         BlendedPosition += TransformedPosition * Weight;
      }
   }

   return BlendedPosition;
}

FVector USkinnedMeshComponent::SkinVertexNormal(const FSkinnedVertex& InVertex) const
{
   FVector BlendedNormal(0.f, 0.f, 0.f);

   for (int32 Idx = 0; Idx < 4; ++Idx)
   {
      const uint32 BoneIndex = InVertex.BoneIndices[Idx];
      const float Weight = InVertex.BoneWeights[Idx];

      if (Weight > 0.f)
      {
         const FMatrix& SkinMatrix = FinalSkinningNormalMatrices[BoneIndex];
         FVector TransformedNormal = SkinMatrix.TransformVector(InVertex.Normal);
         BlendedNormal += TransformedNormal * Weight;
      }
   }

   return BlendedNormal.GetSafeNormal();
}

FVector4 USkinnedMeshComponent::SkinVertexTangent(const FSkinnedVertex& InVertex) const
{
   const FVector OriginalTangentDir(InVertex.Tangent.X, InVertex.Tangent.Y, InVertex.Tangent.Z);
   const float OriginalSignW = InVertex.Tangent.W;

   FVector BlendedTangentDir(0.f, 0.f, 0.f);

   for (int32 Idx = 0; Idx < 4; ++Idx)
   {
      const uint32 BoneIndex = InVertex.BoneIndices[Idx];
      const float Weight = InVertex.BoneWeights[Idx];

      if (Weight > 0.f)
      {
         const FMatrix& SkinMatrix = FinalSkinningMatrices[BoneIndex];
         FVector TransformedTangentDir = SkinMatrix.TransformVector(OriginalTangentDir);
         BlendedTangentDir += TransformedTangentDir * Weight;
      }
   }

   const FVector FinalTangentDir = BlendedTangentDir.GetSafeNormal();
   return { FinalTangentDir.X, FinalTangentDir.Y, FinalTangentDir.Z, OriginalSignW };
}

void USkinnedMeshComponent::InitializeGPUSkinningResource()
{
   uint32 BoneCount = SkeletalMesh->GetBoneCount();
   if (BoneCount <= 0)
   {
      return;
   }
   SkeletalMesh->CreateCPUSkinnedVertexBuffer(&CPUSkinnedVertexBuffer);
   SkeletalMesh->CreateGPUSkinnedVertexBuffer(&GPUSkinnedVertexBuffer);

   if (!CSVertexBuffer)
   {
      SkeletalMesh->CreateCSInputBuffer(&CSVertexBuffer, &CSVertexSRV, SkeletalMesh->GetVertexCount());
   }

   if (!CSOutputBuffer)
   {
      SkeletalMesh->CreateCSOutputBuffer(&CSOutputBuffer, &CSOutputVertexUAV, &CSOutVertexSRV, SkeletalMesh->GetVertexCount());
   }

   if (!SkinningMatrixBuffer)
   {
      SkeletalMesh->CreateStructuredBuffer(&SkinningMatrixBuffer, &SkinningMatrixSRV, BoneCount);
   }

   if (!SkinningNormalMatrixBuffer)
   {
      SkeletalMesh->CreateStructuredBuffer(&SkinningNormalMatrixBuffer, &SkinningNormalMatrixSRV, BoneCount);
   }

   if (!SkinningCS)
   {
      SkinningCS = UResourceManager::GetInstance().Load<UShader>("Shaders/Skinning/SkinnedMesh_CS.hlsl");
   }

   bIsResourcePrepare = CSVertexBuffer && CSVertexSRV &&
         CSOutputBuffer && CSOutputVertexUAV &&
         CSOutVertexSRV && SkinningMatrixBuffer &&
         SkinningMatrixSRV && SkinningNormalMatrixBuffer && SkinningNormalMatrixSRV && SkinningCS;
}
