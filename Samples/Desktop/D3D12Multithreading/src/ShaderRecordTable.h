#pragma once
#include <d3d12.h>
#include "StateObject.h"

template<typename T>
inline T align(T size, T alignment)
{
    return (size + (alignment - 1)) & ~(alignment - 1);
}

/// @brief get shader record stride with argument size
///
/// @param argumentSize : size of arguments
///
/// @return shader record aligned stride in bytes
///
inline size_t GetShaderRecordStride(size_t argumentSize)
{
    size_t size = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT + argumentSize;
    return align(size, static_cast<size_t>(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT));
}

/// @brief shader record table
///
class ShaderRecordTable
{
public:

    /// @brief construct shader record table
    ///
    /// @param pso      : pipeline state object to associate with
    /// @param capacity : maximum record count
    /// @param stride   : per-record argument data stride
    ///
    explicit ShaderRecordTable(
        const StateObject& pso, size_t capacity, size_t stride = 1);

    /// @brief release shader record table
    ///
    ~ShaderRecordTable();

    /// @brief returns shader record table GPU virtual address
    ///
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

    /// @brief retrieve raw data pointer
    ///
    void* GetData() const;

    /// @brief get shader record table filled size in bytes
    ///
    size_t SizeInBytes() const;

    /// @brief get shader record stride in bytes
    ///
    size_t StrideInBytes() const;

    /// @brief push back shader record with optional arguments
    ///
    /// @param pExport    : shader export. must exist in attaches pso
    /// @param pArguments : optional argument data
    /// @param size       : argument data size
    ///
    void PushBack(
        const wchar_t* pExport, const void* pArguments = 0, size_t size = 0);

    /// @brief allocate gpu shader record table
    ///
    void Alloc(ID3D12Device* pDevice);

private:

    /// private member variables
    byte*                                   m_pData;
    byte*                                   m_pCurrent;
    size_t                                  m_capacity;
    size_t                                  m_stride;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_resource;
    const StateObject&                      m_pso;
};
