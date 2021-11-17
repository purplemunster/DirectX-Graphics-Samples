#include "stdafx.h"
#include "AccelerationStructure.h"
#include "DXSampleHelper.h"

#if _DEBUG
#define DEBUG_ACCEL 1
#endif

//=====================================================================================================================
void AccelerationStructure::Create(
    ID3D12Device5* pDevice,
    size_t         maxInstanceCount,
    size_t         maxFrameCount)
{
    m_device = pDevice;
}

//=====================================================================================================================
void AccelerationStructure::AddGeometry(
    const std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geometry, bool bAllowUpdate, bool bAllowCompaction)
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInput = {};
    blasInput.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blasInput.pGeometryDescs = geometry.data();
    blasInput.NumDescs       = static_cast<uint32_t>(geometry.size());
    blasInput.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blasInput.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    if (bAllowUpdate)
    {
        blasInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    }

    if (bAllowCompaction)
    {
        blasInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasBuildInfo = {};
    m_device->GetRaytracingAccelerationStructurePrebuildInfo(&blasInput, &blasBuildInfo);
    assert(blasBuildInfo.ResultDataMaxSizeInBytes != 0);

    AccelStructResource blas = {};

    D3D12_RESOURCE_DESC bufferDesc =
        CD3DX12_RESOURCE_DESC::Buffer(blasBuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

#if DEBUG_ACCEL
    blas.result.resource = CreateMapDefaultBuffer(
        m_device.Get(), blasBuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    blas.result.resource->Map(0, 0, &blas.result.pMappedCpuPtr);
#else
    blas.result.resource = CreateBuffer(
        m_device.Get(),
        blasBuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#endif

    blas.resultSize  = blasBuildInfo.ResultDataMaxSizeInBytes;
    blas.scratchSize = blasBuildInfo.ScratchDataSizeInBytes;
    blas.updateSize  = blasBuildInfo.UpdateScratchDataSizeInBytes;
    blas.compact     = bAllowCompaction;

#if DEBUG_ACCEL
    if (blas.updateSize > 0)
    {
        blas.update.resource = CreateMapDefaultBuffer(m_device.Get(), blas.updateSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        blas.update.resource->Map(0, 0, &blas.update.pMappedCpuPtr);
    }

    blas.scratch.resource = CreateMapDefaultBuffer(m_device.Get(), blas.scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    blas.scratch.resource->Map(0, 0, &blas.scratch.pMappedCpuPtr);
#else
    blas.scratch.resource = CreateBuffer(
        m_device.Get(),
        blas.scratchSize,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    if (blas.updateSize > 0)
    {
        blas.update.resource = CreateBuffer(
            m_device.Get(),
            blas.updateSize,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
#endif

    m_geometries.emplace_back(std::make_pair(blas, geometry));
}

//=====================================================================================================================
void AccelerationStructure::AddInstance(
    uint32_t                        geometryIdx,
    const DirectX::XMMATRIX&        M,
    D3D12_RAYTRACING_INSTANCE_FLAGS flags,
    uint32_t                        instanceID,
    uint32_t                        hitGroupOffset,
    uint32_t                        instanceMask)
{
    const uint32_t instanceId = (instanceID == 0xffffffff) ? static_cast<uint32_t>(m_instances.size()) : instanceID;

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};

    if (geometryIdx != -1)
    {
        instanceDesc.AccelerationStructure = m_geometries[geometryIdx].first.result.resource->GetGPUVirtualAddress();
    }

    instanceDesc.Flags                               = flags;
    instanceDesc.InstanceID                          = instanceId;
    instanceDesc.InstanceContributionToHitGroupIndex = hitGroupOffset;
    instanceDesc.InstanceMask                        = instanceMask;
    XMStoreFloat3x4((DirectX::XMFLOAT3X4*)(instanceDesc.Transform), M);

    m_instances.emplace_back(std::make_pair(geometryIdx, instanceDesc));
}

//=====================================================================================================================
void AccelerationStructure::UpdateInstance(
    uint32_t instanceIdx, const DirectX::XMMATRIX& M)
{
    if (m_pGpuInstances &&  (instanceIdx < m_instances.size()))
    {
        XMStoreFloat3x4((DirectX::XMFLOAT3X4*)(m_pGpuInstances[instanceIdx].Transform), M);
    }
}

//=====================================================================================================================
void AccelerationStructure::UpdateInstance(
    uint32_t instanceIdx, D3D12_GPU_VIRTUAL_ADDRESS blasGpuVa)
{
    if (m_pGpuInstances && (instanceIdx < m_instances.size()))
    {
        m_pGpuInstances[instanceIdx].AccelerationStructure = blasGpuVa;
    }
}

//=====================================================================================================================
void AccelerationStructure::Build(
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList, bool bAllowUpdate, bool bAllowCompaction)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 data = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &data, sizeof(data));

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4 = nullptr;
    ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList4)));

    // create instance buffer
    size_t instanceSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_instances.size();
    m_instanceBuffer.resource = CreateUploadBuffer(m_device.Get(), instanceSize);

    // copy instance data
    ThrowIfFailed(m_instanceBuffer.resource->Map(
        0, &CD3DX12_RANGE(0, instanceSize), reinterpret_cast<void**>(&m_pGpuInstances)));
    for (uint32_t i = 0; i < m_instances.size(); ++i)
    {
        m_pGpuInstances[i] = m_instances[i].second;
    }

    const size_t emitSizeInBytes =
        (m_geometries.size() + 1) * sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC);
    auto emitResource = CreateMapDefaultBuffer(m_device.Get(), emitSizeInBytes, D3D12_RESOURCE_STATE_GENERIC_READ);

    // Allocate timestamp query heap for each build item
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Count = 2 * static_cast<UINT>(m_geometries.size() + m_instances.size());
    desc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

    ThrowIfFailed(m_device->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_tsQueryHeap)));

    m_tsResultBuffer.resource = CreateReadbackBuffer(m_device.Get(), desc.Count * sizeof(uint64_t));
    m_tsResultBuffer.resource->Map(0, 0, &m_tsResultBuffer.pMappedCpuPtr);

    // gather tlas scratch buffer size and update
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInput = {};
    tlasInput.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInput.NumDescs      = static_cast<uint32_t>(m_instances.size());
    tlasInput.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInput.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInput.InstanceDescs = m_instanceBuffer.resource->GetGPUVirtualAddress();

    if (bAllowUpdate)
    {
        tlasInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    }

    if (bAllowCompaction)
    {
        tlasInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasBuildInfo = {};
    m_device->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInput, &tlasBuildInfo);
    assert(tlasBuildInfo.ResultDataMaxSizeInBytes != 0);

    // update and allocate scratch buffer
    m_tlas.scratchSize = max(tlasBuildInfo.ScratchDataSizeInBytes, tlasBuildInfo.UpdateScratchDataSizeInBytes);
    m_tlas.resultSize = tlasBuildInfo.ResultDataMaxSizeInBytes;
    m_tlas.scratch.resource = CreateMapDefaultBuffer(m_device.Get(), m_tlas.scratchSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_tlas.compact = bAllowCompaction;

    // allocate tlas buffer
#if DEBUG_ACCEL
    m_tlas.result.resource = CreateMapDefaultBuffer(m_device.Get(), tlasBuildInfo.ResultDataMaxSizeInBytes,
                                                          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    m_tlas.result.resource->Map(0, 0, &m_tlas.result.pMappedCpuPtr);
#else
    m_tlas.result.resource = CreateBuffer(
        m_device.Get(),
        tlasBuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#endif

    struct TsData
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build;
        size_t queryIndex;
    };

    std::vector<TsData> timestamps;

    size_t emitOffset = 0;

    // Build bottom-level acceleration structures
    for (uint32_t i = 0; i < m_geometries.size(); ++i)
    {
        const auto& geometry = m_geometries[i].second;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInput = {};
        blasInput.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        blasInput.pGeometryDescs = geometry.data();
        blasInput.NumDescs       = static_cast<uint32_t>(geometry.size());
        blasInput.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        blasInput.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        if (m_geometries[i].first.updateSize != 0)
        {
            blasInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }

        if (m_geometries[i].first.compact)
        {
            blasInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
        }

        // build bottom level acceleration structures
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        ZeroMemory(&buildDesc, sizeof(buildDesc));

        buildDesc.Inputs                           = blasInput;
        buildDesc.DestAccelerationStructureData    = m_geometries[i].first.result.resource->GetGPUVirtualAddress();
        buildDesc.ScratchAccelerationStructureData = m_geometries[i].first.scratch.resource->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postBuildInfo = {};
        postBuildInfo.DestBuffer = emitResource->GetGPUVirtualAddress() + emitOffset;
        postBuildInfo.InfoType   = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;

        const uint32_t queryIndex = static_cast<uint32_t>(2 * timestamps.size());
        commandList4->EndQuery(m_tsQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
        commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 1, &postBuildInfo);
        commandList4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_geometries[i].first.result.resource.Get()));
        commandList4->EndQuery(m_tsQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex + 1);
        timestamps.push_back({ buildDesc, timestamps.size() });

        emitOffset += sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC);
    }

    // build top level acceleration structure
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    ZeroMemory(&buildDesc, sizeof(buildDesc));
    buildDesc.Inputs                           = tlasInput;
    buildDesc.DestAccelerationStructureData    = m_tlas.result.resource->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = m_tlas.scratch.resource->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postBuildInfo = {};
    postBuildInfo.DestBuffer = emitResource->GetGPUVirtualAddress() + emitOffset;
    postBuildInfo.InfoType   = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;

    const uint32_t queryIndex = static_cast<uint32_t>(2 * timestamps.size());
    commandList4->EndQuery(m_tsQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
    commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 1, &postBuildInfo);
    commandList4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.result.resource.Get()));
    commandList4->EndQuery(m_tsQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex + 1);
    timestamps.push_back({ buildDesc, queryIndex });

    // resolve timestampts
    const uint32_t resolveQuerySize = static_cast<uint32_t>(2 * timestamps.size());
    commandList4->ResolveQueryData(m_tsQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, resolveQuerySize, m_tsResultBuffer.resource.Get(), 0);

    m_view.Format                                   = DXGI_FORMAT_UNKNOWN;
    m_view.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    m_view.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_view.RaytracingAccelerationStructure.Location = m_tlas.result.resource->GetGPUVirtualAddress();
}

//=====================================================================================================================
void AccelerationStructure::PerformUpdate(
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 data = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &data, sizeof(data));

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4 = nullptr;
    ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList4)));

    // gather tlas scratch buffer size and update
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInput = {};
    tlasInput.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInput.NumDescs      = static_cast<uint32_t>(m_instances.size());
    tlasInput.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInput.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                              D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE |
                              D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    tlasInput.InstanceDescs = m_instanceBuffer.resource->GetGPUVirtualAddress();

    // build top level acceleration structure
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    ZeroMemory(&buildDesc, sizeof(buildDesc));
    buildDesc.Inputs                           = tlasInput;
    buildDesc.SourceAccelerationStructureData  = m_tlas.result.resource->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData    = m_tlas.result.resource->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = m_tlas.scratch.resource->GetGPUVirtualAddress();

    commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    commandList4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.result.resource.Get()));
}

//=====================================================================================================================
void AccelerationStructure::PerformRebuild(
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 data = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &data, sizeof(data));

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4 = nullptr;
    ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList4)));

    // gather tlas scratch buffer size and update
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInput = {};
    tlasInput.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInput.NumDescs      = static_cast<uint32_t>(m_instances.size());
    tlasInput.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInput.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInput.InstanceDescs = m_instanceBuffer.resource->GetGPUVirtualAddress();

    // build top level acceleration structure
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    ZeroMemory(&buildDesc, sizeof(buildDesc));
    buildDesc.Inputs                           = tlasInput;
    buildDesc.DestAccelerationStructureData    = m_tlas.result.resource->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = m_tlas.scratch.resource->GetGPUVirtualAddress();

    commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    commandList4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.result.resource.Get()));
}

//=====================================================================================================================
void AccelerationStructure::PerformUpdateGeometry(
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 data = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &data, sizeof(data));

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4 = nullptr;
    ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList4)));

    std::vector<D3D12_RESOURCE_BARRIER> barriers(m_geometries.size());
    for (uint32_t i = 0; i < m_geometries.size(); ++i)
    {
        const auto& geometry = m_geometries[i].second;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInput = {};
        blasInput.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        blasInput.pGeometryDescs = geometry.data();
        blasInput.NumDescs       = static_cast<uint32_t>(geometry.size());
        blasInput.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        blasInput.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

        // build bottom level acceleration structures
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        ZeroMemory(&buildDesc, sizeof(buildDesc));

        buildDesc.Inputs                           = blasInput;
        buildDesc.DestAccelerationStructureData    = m_geometries[i].first.result.resource->GetGPUVirtualAddress();
        buildDesc.SourceAccelerationStructureData  = m_geometries[i].first.result.resource->GetGPUVirtualAddress();
        buildDesc.ScratchAccelerationStructureData = m_geometries[i].first.update.resource->GetGPUVirtualAddress();

        commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        barriers[i] = CD3DX12_RESOURCE_BARRIER::UAV(m_geometries[i].first.result.resource.Get());
    }

    commandList4->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

//=====================================================================================================================
void AccelerationStructure::PerformRebuildGeometry(
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 data = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &data, sizeof(data));

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4 = nullptr;
    ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList4)));

    std::vector<D3D12_RESOURCE_BARRIER> barriers(m_geometries.size());
    for (uint32_t i = 0; i < m_geometries.size(); ++i)
    {
        const auto& geometry = m_geometries[i].second;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInput = {};
        blasInput.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        blasInput.pGeometryDescs = geometry.data();
        blasInput.NumDescs       = static_cast<uint32_t>(geometry.size());
        blasInput.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        blasInput.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        // build bottom level acceleration structures
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        ZeroMemory(&buildDesc, sizeof(buildDesc));

        buildDesc.Inputs                           = blasInput;
        buildDesc.DestAccelerationStructureData    = m_geometries[i].first.result.resource->GetGPUVirtualAddress();
        buildDesc.ScratchAccelerationStructureData = m_geometries[i].first.scratch.resource->GetGPUVirtualAddress();

        commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        barriers[i] = CD3DX12_RESOURCE_BARRIER::UAV(m_geometries[i].first.result.resource.Get());
    }

    commandList4->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

//=====================================================================================================================
void AccelerationStructure::PerformCompaction(
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 data = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &data, sizeof(data));

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4 = nullptr;
    ThrowIfFailed(commandList->QueryInterface(IID_PPV_ARGS(&commandList4)));

    // Allocate compaction buffers
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> compactedBuffers(m_geometries.size());

    std::vector<D3D12_RESOURCE_BARRIER> barriers(m_geometries.size());
    for (uint32_t i = 0; i < m_geometries.size(); ++i)
    {
        if (m_geometries[i].first.compact)
        {
            // Allocate compacted buffer
#if DEBUG_ACCEL
            compactedBuffers[i] = CreateMapDefaultBuffer(m_device.Get(), m_geometries[i].first.compactSize,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
#else
            compactedBuffers[i] = CreateBuffer(m_device.Get(), m_geometries[i].first.compactSize,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                0,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#endif

            // Queue copy commands
            commandList4->CopyRaytracingAccelerationStructure(compactedBuffers[i]->GetGPUVirtualAddress(),
                                                              m_geometries[i].first.result.resource->GetGPUVirtualAddress(),
                                                              D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
        }
    }

    for (uint32_t i = 0; i < m_instances.size(); ++i)
    {
        if (m_pGpuInstances[i].AccelerationStructure != 0)
        {
            // update tlas reference
            m_pGpuInstances[i].AccelerationStructure = compactedBuffers[m_instances[i].first]->GetGPUVirtualAddress();
        }
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInput = {};
    tlasInput.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInput.NumDescs      = static_cast<uint32_t>(m_instances.size());
    tlasInput.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInput.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInput.InstanceDescs = m_instanceBuffer.resource->GetGPUVirtualAddress();

    // re-build top level acceleration structure
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    ZeroMemory(&buildDesc, sizeof(buildDesc));
    buildDesc.Inputs = tlasInput;
    buildDesc.DestAccelerationStructureData = m_tlas.result.resource->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = m_tlas.scratch.resource->GetGPUVirtualAddress();

    commandList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    commandList4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_tlas.result.resource.Get()));

    Microsoft::WRL::ComPtr<ID3D12Resource> compactedTlas = nullptr;
    if (m_tlas.compact)
    {
        // Allocate compacted buffer
#if DEBUG_ACCEL
        compactedTlas = CreateMapDefaultBuffer(m_device.Get(), m_tlas.compactSize,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
#else
        compactedTlas = CreateBuffer(
            m_device.Get(),
            m_tlas.compactSize,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#endif
        // Queue copy commands
        commandList4->CopyRaytracingAccelerationStructure(
            compactedTlas->GetGPUVirtualAddress(),
            m_tlas.result.resource->GetGPUVirtualAddress(),
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
        commandList4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(compactedTlas.Get()));
    }
}