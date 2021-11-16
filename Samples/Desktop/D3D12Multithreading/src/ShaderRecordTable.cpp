#include "stdafx.h"
#include "ShaderRecordTable.h"
#include "DXSampleHelper.h"

//=====================================================================================================================
ShaderRecordTable::ShaderRecordTable(
    const StateObject&    pso,
    size_t                capacity,
    size_t                stride)
    :
    m_pso(pso),
    m_pData(nullptr),
    m_pCurrent(nullptr),
    m_capacity(0),
    m_stride(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT)
{
    m_stride   += align(stride, static_cast<size_t>(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));
    m_capacity  = align(capacity * m_stride, static_cast<size_t>(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT));
    m_pData     = (byte*)malloc(m_capacity);
    m_pCurrent  = m_pData;
}

//=====================================================================================================================
D3D12_GPU_VIRTUAL_ADDRESS ShaderRecordTable::GetGPUVirtualAddress() const
{
    return m_resource->GetGPUVirtualAddress();
}

//=====================================================================================================================
ShaderRecordTable::~ShaderRecordTable()
{
    if (m_pData != nullptr)
    {
        free(m_pData);
    }
}

//=====================================================================================================================
void* ShaderRecordTable::GetData() const
{
    return m_pData;
}

//=====================================================================================================================
size_t ShaderRecordTable::SizeInBytes() const
{
    return m_capacity;
}

//=====================================================================================================================
size_t ShaderRecordTable::StrideInBytes() const
{
    return m_stride;
}

//=====================================================================================================================
void ShaderRecordTable::PushBack(
    const wchar_t* pExport, const void* pArguments, size_t size)
{
    if (pExport == nullptr)
    {
        throw HrException(E_INVALIDARG);
    }

    if ((pArguments != nullptr) && (size == 0))
    {
        throw HrException(E_INVALIDARG);
    }

    void* pShaderIdentifier = m_pso.GetShaderIdentifier(pExport);
    if (pShaderIdentifier == nullptr)
    {
        throw HrException(E_NOINTERFACE);
    }

    memcpy(m_pCurrent, pShaderIdentifier, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    if (pArguments != nullptr)
    {
        memcpy(&m_pCurrent[D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT], pArguments, size);
    }

    m_pCurrent += m_stride;
}

//=====================================================================================================================
void ShaderRecordTable::Alloc(
    ID3D12Device* pDevice)
{
    // allocate table memory
    m_resource = CreateBuffer(pDevice, m_capacity, D3D12_RESOURCE_STATE_GENERIC_READ, m_pData, D3D12_RESOURCE_FLAG_NONE);

    // free system memory
    if (m_pData)
    {
        free(m_pData);
        m_pData    = nullptr;
        m_pCurrent = nullptr;
    }
}