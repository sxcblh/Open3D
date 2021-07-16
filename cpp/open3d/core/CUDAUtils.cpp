// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018-2021 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/core/CUDAUtils.h"

#include "open3d/Macro.h"
#include "open3d/utility/Logging.h"

#ifdef BUILD_CUDA_MODULE
#include "open3d/core/CUDAState.cuh"
#include "open3d/core/MemoryManager.h"
#endif

namespace open3d {
namespace core {

#ifdef BUILD_CUDA_MODULE
int GetCUDACurrentDeviceTextureAlignment() {
    int value = 0;
    cudaError_t err = cudaDeviceGetAttribute(
            &value, cudaDevAttrTextureAlignment, cuda::GetDevice());
    if (err != cudaSuccess) {
        utility::LogError(
                "GetCUDACurrentDeviceTextureAlignment(): "
                "cudaDeviceGetAttribute failed with {}",
                cudaGetErrorString(err));
    }
    return value;
}
#endif

namespace cuda {

int DeviceCount() {
#ifdef BUILD_CUDA_MODULE
    try {
        std::shared_ptr<CUDAState> cuda_state = CUDAState::GetInstance();
        return cuda_state->GetNumDevices();
    } catch (const std::runtime_error&) {  // GetInstance can throw
        return 0;
    }
#else
    return 0;
#endif
}

bool IsAvailable() { return cuda::DeviceCount() > 0; }

void ReleaseCache() {
#ifdef BUILD_CUDA_MODULE
#ifdef BUILD_CACHED_CUDA_MANAGER
    // Release cache from all devices. Since only memory from CUDAMemoryManager
    // is cached at the moment, this works as expected. In the future, the logic
    // could become more fine-grained.
    CachedMemoryManager::ReleaseCache();
#else
    utility::LogWarning(
            "Built without cached CUDA memory manager, cuda::ReleaseCache() "
            "has no effect.");
#endif

#else
    utility::LogWarning("Built without CUDA module, cuda::ReleaseCache().");
#endif
}

#ifdef BUILD_CUDA_MODULE

int GetDevice() {
    int device;
    OPEN3D_CUDA_CHECK(cudaGetDevice(&device));
    return device;
}

void SetDevice(int device_id) { OPEN3D_CUDA_CHECK(cudaSetDevice(device_id)); }

class CUDAStream {
public:
    static CUDAStream& GetInstance() {
        // The global stream state is given per thread like CUDA's internal
        // device state.
        static thread_local CUDAStream instance;
        return instance;
    }

    cudaStream_t Get() { return stream_; }
    void Set(cudaStream_t stream) { stream_ = stream; }

    static cudaStream_t Default() { return static_cast<cudaStream_t>(0); }

private:
    CUDAStream() = default;

    cudaStream_t stream_ = Default();
};

cudaStream_t GetStream() { return CUDAStream::GetInstance().Get(); }

void SetStream(cudaStream_t stream) { CUDAStream::GetInstance().Set(stream); }

cudaStream_t GetDefaultStream() { return CUDAStream::Default(); }

#endif

}  // namespace cuda
}  // namespace core
}  // namespace open3d

// C interface to provide un-mangled function to Python ctypes
extern "C" OPEN3D_DLL_EXPORT int open3d_core_cuda_device_count() {
    return open3d::core::cuda::DeviceCount();
}
