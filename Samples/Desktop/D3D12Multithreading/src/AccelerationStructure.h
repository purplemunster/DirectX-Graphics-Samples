#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <vector>

/// @brief acceleration structure wrapper
///
class AccelerationStructure
{
public:

    /// @brief construct acceleration structure
    ///
    explicit AccelerationStructure() { }

    /// @brief release acceleration structure
    ///
    ~AccelerationStructure() { }

    /// @brief create acceleration structure
    ///
    void Create(ID3D12Device5* pDevice, size_t maxInstanceCount, size_t maxFrameCount = 1);

    /// @brief add bottom level geometry
    ///
    /// @param geometry   : geometry descriptors represent a single bottom level
    ///
    void AddGeometry(const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geometry, bool bAllowUpdate = false, bool bAllowCompaction = false);

    /// @brief add top-level instance
    ///
    /// @param geometryIdx    : geometry index
    /// @param M              : instance transform
    /// @param flags          : instance flags
    /// @param hitGroupOffset : hit group offset
    ///
    void AddInstance(uint32_t                        geometryIdx,
                     const DirectX::XMMATRIX&        M              = DirectX::XMMatrixIdentity(),
                     D3D12_RAYTRACING_INSTANCE_FLAGS flags          = D3D12_RAYTRACING_INSTANCE_FLAG_NONE,
                     uint32_t                        instanceID     = 0xffffffff,
                     uint32_t                        hitGroupOffset = 0,
                     uint32_t                        instanceMask   = 0xff);

    /// @brief update instance
    ///
    void UpdateInstance(uint32_t instanceIdx, const DirectX::XMMATRIX& M);

    /// @brief update instance
    ///
    void UpdateInstance(uint32_t instanceIdx, D3D12_GPU_VIRTUAL_ADDRESS blasGpuVa);

    /// @brief build acceleration structure
    ///
    void Build(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList, bool bAllowUpdate = false, bool bAllowCompaction = false);

    /// @brief update acceleration structure
    ///
    void PerformUpdate(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList);

    /// @brief update acceleration structure geometry (BLAS')
    ///
    void PerformUpdateGeometry(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList);

    /// @brief Rebuild acceleration structure
    ///
    void PerformRebuild(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList);

    /// @brief Rebuild acceleration structure from geometry
    ///
    void PerformRebuildGeometry(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList);

    /// @brief Compact acceleration structure
    ///
    void PerformCompaction(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList);

    /// @brief get acceleration structure view
    ///
    const D3D12_SHADER_RESOURCE_VIEW_DESC* GetViewDesc() const {
        return &m_view;
    }

    D3D12_GPU_VIRTUAL_ADDRESS GetInstanceTransform() const {
        return m_instanceBuffer.resource->GetGPUVirtualAddress();
    }

private:

    struct MappableResource
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
        void* pMappedCpuPtr = nullptr;
    };

    struct AccelStructResource
    {
        MappableResource result      = {};
        MappableResource scratch     = {};
        MappableResource update      = {};
        size_t           resultSize  = 0;
        size_t           scratchSize = 0;
        size_t           updateSize  = 0;
        size_t           compactSize = 0;
        bool             compact     = false;
    };

    /// private member variables
    AccelStructResource             m_tlas           = { nullptr }; ///< top-level acceleration structure resource
    MappableResource                m_instanceBuffer = { nullptr }; ///< instance buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC m_view           = {};          ///< view
    uint32_t                        m_frameIndex     = 0;

    typedef std::pair<AccelStructResource, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> BottomLevel;
    std::vector<BottomLevel> m_geometries;

    typedef std::pair<uint32_t, D3D12_RAYTRACING_INSTANCE_DESC> TopLevel;
    std::vector<TopLevel> m_instances;

    D3D12_RAYTRACING_INSTANCE_DESC* m_pGpuInstances;

    MappableResource m_tsResultBuffer = { nullptr, nullptr };
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_tsQueryHeap = {};

    Microsoft::WRL::ComPtr<ID3D12Device5> m_device = {};
};