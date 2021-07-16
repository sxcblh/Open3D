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

#pragma once

#include <cstdint>
#include <vector>

#include "open3d/utility/Parallel.h"

namespace open3d {
namespace core {
namespace kernel {
namespace cpu_launcher {

/// The value is chosen heuristically for small element-wise ops. When the
/// number of workloads is smaller or equal to SMALL_OP_GRAIN_SIZE, the
/// workloads are executed in serial, otherwise they are executed in parallel.
static constexpr int64_t SMALL_OP_GRAIN_SIZE = 32767;

/// \brief Run a function in parallel on CPU.
///
/// This is typically used together with cuda_launcher::ParallelFor() to
/// share the same code between CPU and CUDA. For example:
///
/// ```cpp
/// #if defined(__CUDACC__)
///     namespace launcher = core::kernel::cuda_launcher;
/// #else
///     namespace launcher = core::kernel::cpu_launcher;
/// #endif
///
/// launcher::ParallelFor(num_workloads, [=] OPEN3D_DEVICE(int64_t idx) {
///     process_workload(idx);
/// });
/// ```
///
/// \param n The number of workloads.
/// \param func The function to be executed in parallel. The function should
/// take an int64_t workload index and returns void, i.e., `void func(int64_t)`.
///
/// \note This is optimized for uniform work items, i.e. where each call to \p
/// func takes the same time.
/// \note If you use a lambda function, capture only the required variables
/// instead of all to prevent accidental race conditions. If you want the kernel
/// to be used on both CPU and CUDA, capture the variables by value.
template <typename func_t>
void ParallelFor(int64_t n, const func_t& func) {
#pragma omp parallel for num_threads(utility::EstimateMaxThreads())
    for (int64_t i = 0; i < n; ++i) {
        func(i);
    }
}

/// Run a function in parallel on CPU when the number of workloads is larger
/// than a threshold.
///
/// \param n The number of workloads.
/// \param grain_size If \p n <= \p grain_size, the jobs will be executed in
/// serial.
/// \param func The function to be executed in parallel. The function should
/// take an int64_t workload index and returns void, i.e., `void func(int64_t)`.
template <typename func_t>
void ParallelFor(int64_t n, int64_t grain_size, const func_t& func) {
#pragma omp parallel for schedule(static) if (n > grain_size) \
        num_threads(utility::EstimateMaxThreads())
    for (int64_t i = 0; i < n; ++i) {
        func(i);
    }
}

}  // namespace cpu_launcher
}  // namespace kernel
}  // namespace core
}  // namespace open3d
