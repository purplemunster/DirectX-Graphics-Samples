#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <stdint.h>
#include "d3dx12.h"

class StateObject
{
public:

    /// @brief construct DXR pipeline object
    ///
    explicit StateObject(D3D12_STATE_OBJECT_TYPE type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    /// @brief release pipeline object
    ///
    ~StateObject();

    /// @brief set global root signature
    ///
    /// @param rootSignature : global root signature
    ///
    void SetGlobalRootSignature(Microsoft::WRL::ComPtr<ID3D12RootSignature> const& rootSignature) noexcept;

    /// @brief Set library with optional exports. Defaults to export all
    ///
    /// @param library      : library source code
    /// @param pExportName  : optional export name
    ///
    void SetLibraryExport(D3D12_SHADER_BYTECODE library,
                          const wchar_t*         pExportName = nullptr,
                          const wchar_t*         pRename     = nullptr,
                          ID3D12RootSignature*   pGRS        = nullptr,
                          ID3D12RootSignature*   pLRS        = nullptr) noexcept;

    /// @brief Set shader configuration
    ///
    /// @param maxPayloadSizeInBytes   : payload size in bytes
    /// @param maxAttributeSizeInBytes : attribute size in bytes (max : 32bytes)
    void SetShaderConfig(uint32_t maxPayloadSizeInBytes, uint32_t maxAttributeSizeInBytes = 8) noexcept;

    /// @brief Set state object flags
    void SetStateObjectFlags(D3D12_STATE_OBJECT_FLAGS flags) noexcept;

    /// @brief Set pipeline configuration
    ///
    /// @param maxRecursionDepth : max trace recursion depth
    ///
    void SetPipelineConfig(uint32_t maxRecursionDepth = 1) noexcept;

    /// @brief Add hitgroup
    ///
    /// @param type          : hit group tyoe
    /// @param pHitGroup     : hit group name
    /// @param pClosestHit   : closest hit shader export
    /// @param pAnyHit       : anyhit shader export
    /// @param pIntersection : custom intersection shader export
    ///                        (type must be D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE)
    void AddHitGroup(D3D12_HIT_GROUP_TYPE type,
                     const wchar_t*       pHitGroup,
                     const wchar_t*       pClosestHit,
                     const wchar_t*       pAnyHit       = nullptr,
                     const wchar_t*       pIntersection = nullptr);

    /// @brief Set local root signature with exports
    ///
    /// @param rootSignature : local root signature
    /// @param pShaderExport : optional shader export to associate with
    ///
    void SetLocalRootSignature(Microsoft::WRL::ComPtr<ID3D12RootSignature> const& rootSignature,
                               const wchar_t* pShaderExport = nullptr) noexcept;

    /// @brief Import existing collection
    ///
    void ImportExistingCollection(StateObject* pCollection, const wchar_t* pExportName = nullptr, const wchar_t* pRename = nullptr) noexcept;

    /// @brief Set flags
    void SetFlags(D3D12_STATE_OBJECT_FLAGS flags) noexcept;

    /// @brief compile state object
    ///
    void Compile(ID3D12Device5* pDevice);

    /// @brief Grow compile state object
    ///
    void Grow(ID3D12Device7* pDevice, const StateObject* pParentStateObject);

    /// @brief bind state object
    ///
    /// @param commandList : RS5 command list
    ///
    void Bind(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5>& commandList) noexcept;

    /// @brief Retrieve shader identifier
    ///
    /// @param pExportName : export name to retrieve identifier for
    ///
    /// @return pointer to shader export identifier
    ///
    void* GetShaderIdentifier(const wchar_t* pExportName) const;

private:

    /// private member variables
    CD3DX12_STATE_OBJECT_DESC                           m_stream          = {};
    Microsoft::WRL::ComPtr<ID3D12RootSignature>         m_rootSignature   = { nullptr };
    Microsoft::WRL::ComPtr<ID3D12StateObject>           m_stateObject     = { nullptr };
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_stateObjectInfo = { nullptr };
};