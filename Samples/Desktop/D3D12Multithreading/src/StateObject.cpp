#include "stdafx.h"
#include "StateObject.h"
#include "DXSampleHelper.h"

//=====================================================================================================================
StateObject::StateObject(
    D3D12_STATE_OBJECT_TYPE type)
{
    m_stream.SetStateObjectType(type);
}

//=====================================================================================================================
StateObject::~StateObject()
{

}

//=====================================================================================================================
void StateObject::SetGlobalRootSignature(
    Microsoft::WRL::ComPtr<ID3D12RootSignature> const& rootSignature) noexcept
{
    m_rootSignature = rootSignature;

    auto grs = m_stream.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    grs->SetRootSignature(rootSignature.Get());
}

//=====================================================================================================================
void StateObject::SetLibraryExport(
    D3D12_SHADER_BYTECODE   library,
    const wchar_t*          pExportName,
    const wchar_t*          pExportRename,
    ID3D12RootSignature*    pGRS,
    ID3D12RootSignature*    pLRS) noexcept
{
    auto lib = m_stream.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    lib->SetDXILLibrary(&library);
    if (pExportName)
    {
        if (pExportRename != nullptr)
        {
            lib->DefineExport(pExportRename, pExportName);
        }
        else
        {
            lib->DefineExport(pExportName);
        }

        const wchar_t* pName = pExportRename ? pExportRename : pExportName;
        if (pGRS != nullptr)
        {
            auto rs = m_stream.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
            rs->SetRootSignature(pGRS);

            auto association = m_stream.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            association->AddExport(pName);
            association->SetSubobjectToAssociate(*rs);
        }

        if (pLRS != nullptr)
        {
            auto rs = m_stream.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
            rs->SetRootSignature(pLRS);

            auto association = m_stream.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
            association->AddExport(pName);
            association->SetSubobjectToAssociate(*rs);
        }
    }
}

//=====================================================================================================================
void StateObject::SetShaderConfig(
    uint32_t maxPayloadSizeInBytes, uint32_t maxAttributeSizeInBytes) noexcept
{
    auto config = m_stream.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    config->Config(maxPayloadSizeInBytes, maxAttributeSizeInBytes);
}

//=====================================================================================================================
void StateObject::SetStateObjectFlags(
    D3D12_STATE_OBJECT_FLAGS flags) noexcept
{
    auto config = m_stream.CreateSubobject<CD3DX12_STATE_OBJECT_CONFIG_SUBOBJECT>();
    config->SetFlags(flags);
}

//=====================================================================================================================
void StateObject::SetPipelineConfig(
    uint32_t maxRecursionDepth) noexcept
{
    auto pipeline = m_stream.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipeline->Config(maxRecursionDepth);
}

//=====================================================================================================================
void StateObject::AddHitGroup(
    D3D12_HIT_GROUP_TYPE type,
    const wchar_t*       pHitGroup,
    const wchar_t*       pClosestHit,
    const wchar_t*       pAnyHit,
    const wchar_t*       pIntersection)
{
    auto hitGroup = m_stream.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();

    hitGroup->SetHitGroupType(type);
    hitGroup->SetHitGroupExport(pHitGroup);
    hitGroup->SetClosestHitShaderImport(pClosestHit);
    hitGroup->SetAnyHitShaderImport(pAnyHit);

    if (type == D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE)
    {
        if (pIntersection == nullptr)
        {
            ThrowIfFailed(E_INVALIDARG);
        }

        hitGroup->SetIntersectionShaderImport(pIntersection);
    }
}

//=====================================================================================================================
void StateObject::SetLocalRootSignature(
    Microsoft::WRL::ComPtr<ID3D12RootSignature> const& rootSignature,
    const wchar_t* pExport) noexcept
{
    auto lrs = m_stream.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
    lrs->SetRootSignature(rootSignature.Get());

    if (pExport != nullptr)
    {
        auto association = m_stream.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        association->AddExport(pExport);
        association->SetSubobjectToAssociate(*lrs);
    }
}

//=====================================================================================================================
void StateObject::ImportExistingCollection(
    StateObject* pCollection, const wchar_t* pExportName, const wchar_t* pExportRename) noexcept
{
    auto collection = m_stream.CreateSubobject<CD3DX12_EXISTING_COLLECTION_SUBOBJECT>();
    collection->SetExistingCollection(pCollection->m_stateObject.Get());

    if (pExportName)
    {
        if (pExportRename != nullptr)
        {
            collection->DefineExport(pExportRename, pExportName);
        }
        else
        {
            collection->DefineExport(pExportName);
        }
    }
}

//=====================================================================================================================
void StateObject::SetFlags(
    D3D12_STATE_OBJECT_FLAGS flags) noexcept
{
    auto stateObjectConfig = m_stream.CreateSubobject<CD3DX12_STATE_OBJECT_CONFIG_SUBOBJECT>();
    stateObjectConfig->SetFlags(flags);
}

//=====================================================================================================================
void StateObject::Compile(ID3D12Device5* pDevice)
{
    ThrowIfFailed(pDevice->CreateStateObject(&D3D12_STATE_OBJECT_DESC(m_stream), IID_PPV_ARGS(&m_stateObject)));
    ThrowIfFailed(m_stateObject->QueryInterface(IID_PPV_ARGS(&m_stateObjectInfo)));
}

//=====================================================================================================================
void StateObject::Grow(
    ID3D12Device7* pDevice, const StateObject* pParentStateObject)
{
    ThrowIfFailed(pDevice->AddToStateObject(
        &D3D12_STATE_OBJECT_DESC(m_stream), pParentStateObject->m_stateObject.Get(), IID_PPV_ARGS(&m_stateObject)));

    ThrowIfFailed(m_stateObject->QueryInterface(IID_PPV_ARGS(&m_stateObjectInfo)));

    // inherit root signature. Currently this only supports one global root signature
    if (pParentStateObject->m_rootSignature.Get())
    {
        m_rootSignature = pParentStateObject->m_rootSignature.Get();
    }
}

//=====================================================================================================================
void* StateObject::GetShaderIdentifier(const wchar_t* pExportName) const
{
    if (m_stateObjectInfo == nullptr)
    {
        throw std::runtime_error("pipeline state invalid");
    }

    return m_stateObjectInfo->GetShaderIdentifier(pExportName);
}

//=====================================================================================================================
void StateObject::Bind(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5>& commandList) noexcept
{
    commandList->SetComputeRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState1(m_stateObject.Get());
}