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

#include "open3d/core/Dispatch.h"
#include "open3d/core/Dtype.h"
#include "open3d/core/Indexer.h"
#include "open3d/core/MemoryManager.h"
#include "open3d/core/SizeVector.h"
#include "open3d/core/Tensor.h"
#include "open3d/core/kernel/BinaryEW.h"
#include "open3d/core/kernel/CPULauncher.h"
#include "open3d/utility/Logging.h"

namespace open3d {
namespace core {
namespace kernel {

template <typename func_t>
static void LaunchBinaryEWKernel(const Indexer& indexer, const func_t& func) {
    cpu_launcher::ParallelFor(
            indexer.NumWorkloads(), cpu_launcher::SMALL_OP_GRAIN_SIZE,
            [&indexer, &func](int64_t i) {
                func(indexer.GetInputPtr(0, i), indexer.GetInputPtr(1, i),
                     indexer.GetOutputPtr(i));
            });
}

template <typename scalar_t>
static void CPUAddElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<scalar_t*>(dst) = *static_cast<const scalar_t*>(lhs) +
                                   *static_cast<const scalar_t*>(rhs);
}

template <typename scalar_t>
static void CPUSubElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<scalar_t*>(dst) = *static_cast<const scalar_t*>(lhs) -
                                   *static_cast<const scalar_t*>(rhs);
}

template <typename scalar_t>
static void CPUMulElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<scalar_t*>(dst) = *static_cast<const scalar_t*>(lhs) *
                                   *static_cast<const scalar_t*>(rhs);
}

template <typename scalar_t>
static void CPUDivElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<scalar_t*>(dst) = *static_cast<const scalar_t*>(lhs) /
                                   *static_cast<const scalar_t*>(rhs);
}

template <typename src_t, typename dst_t>
static void CPULogicalAndElementKernel(const void* lhs,
                                       const void* rhs,
                                       void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            static_cast<bool>(*static_cast<const src_t*>(lhs)) &&
            static_cast<bool>(*static_cast<const src_t*>(rhs)));
}

template <typename src_t, typename dst_t>
static void CPULogicalOrElementKernel(const void* lhs,
                                      const void* rhs,
                                      void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            static_cast<bool>(*static_cast<const src_t*>(lhs)) ||
            static_cast<bool>(*static_cast<const src_t*>(rhs)));
}

template <typename src_t, typename dst_t>
static void CPULogicalXorElementKernel(const void* lhs,
                                       const void* rhs,
                                       void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            static_cast<bool>(*static_cast<const src_t*>(lhs)) !=
            static_cast<bool>(*static_cast<const src_t*>(rhs)));
}

template <typename src_t, typename dst_t>
static void CPUGtElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            *static_cast<const src_t*>(lhs) > *static_cast<const src_t*>(rhs));
}

template <typename src_t, typename dst_t>
static void CPULtElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            *static_cast<const src_t*>(lhs) < *static_cast<const src_t*>(rhs));
}

template <typename src_t, typename dst_t>
static void CPUGeqElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            *static_cast<const src_t*>(lhs) >= *static_cast<const src_t*>(rhs));
}

template <typename src_t, typename dst_t>
static void CPULeqElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            *static_cast<const src_t*>(lhs) <= *static_cast<const src_t*>(rhs));
}

template <typename src_t, typename dst_t>
static void CPUEqElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            *static_cast<const src_t*>(lhs) == *static_cast<const src_t*>(rhs));
}

template <typename src_t, typename dst_t>
static void CPUNeqElementKernel(const void* lhs, const void* rhs, void* dst) {
    *static_cast<dst_t*>(dst) = static_cast<dst_t>(
            *static_cast<const src_t*>(lhs) != *static_cast<const src_t*>(rhs));
}

template <typename src_t, typename dst_t>
static void LaunchBoolBinaryEWCPUKernel(const Tensor& lhs,
                                        const Tensor& rhs,
                                        Tensor& dst,
                                        BinaryEWOpCode op_code,
                                        const Indexer& indexer) {
    switch (op_code) {
        case BinaryEWOpCode::LogicalAnd:
            LaunchBinaryEWKernel(indexer,
                                 CPULogicalAndElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::LogicalOr:
            LaunchBinaryEWKernel(indexer,
                                 CPULogicalOrElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::LogicalXor:
            LaunchBinaryEWKernel(indexer,
                                 CPULogicalXorElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::Gt:
            LaunchBinaryEWKernel(indexer, CPUGtElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::Lt:
            LaunchBinaryEWKernel(indexer, CPULtElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::Ge:
            LaunchBinaryEWKernel(indexer, CPUGeqElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::Le:
            LaunchBinaryEWKernel(indexer, CPULeqElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::Eq:
            LaunchBinaryEWKernel(indexer, CPUEqElementKernel<src_t, dst_t>);
            break;
        case BinaryEWOpCode::Ne:
            LaunchBinaryEWKernel(indexer, CPUNeqElementKernel<src_t, dst_t>);
            break;
        default:
            break;
    }
}

void BinaryEWCPU(const Tensor& lhs,
                 const Tensor& rhs,
                 Tensor& dst,
                 BinaryEWOpCode op_code) {
    Dtype src_dtype = lhs.GetDtype();
    Dtype dst_dtype = dst.GetDtype();

    if (s_boolean_binary_ew_op_codes.find(op_code) !=
        s_boolean_binary_ew_op_codes.end()) {
        DISPATCH_DTYPE_TO_TEMPLATE_WITH_BOOL(src_dtype, [&]() {
            if (dst_dtype == src_dtype) {
                // Inplace boolean op's output type is the same as the
                // input. e.g. np.logical_and(a, b, out=a), where a, b are
                // floats.
                Indexer indexer({lhs, rhs}, dst, DtypePolicy::ALL_SAME);
                LaunchBoolBinaryEWCPUKernel<scalar_t, scalar_t>(
                        lhs, rhs, dst, op_code, indexer);
            } else if (dst_dtype == Dtype::Bool) {
                // By default, output is boolean type.
                Indexer indexer({lhs, rhs}, dst,
                                DtypePolicy::INPUT_SAME_OUTPUT_BOOL);
                LaunchBoolBinaryEWCPUKernel<scalar_t, bool>(lhs, rhs, dst,
                                                            op_code, indexer);
            } else {
                utility::LogError(
                        "Boolean op's output type must be boolean or the "
                        "same type as the input.");
            }
        });
    } else {
        Indexer indexer({lhs, rhs}, dst, DtypePolicy::ALL_SAME);
        DISPATCH_DTYPE_TO_TEMPLATE(src_dtype, [&]() {
            switch (op_code) {
                case BinaryEWOpCode::Add:
                    LaunchBinaryEWKernel(indexer,
                                         CPUAddElementKernel<scalar_t>);
                    break;
                case BinaryEWOpCode::Sub:
                    LaunchBinaryEWKernel(indexer,
                                         CPUSubElementKernel<scalar_t>);
                    break;
                case BinaryEWOpCode::Mul:
                    LaunchBinaryEWKernel(indexer,
                                         CPUMulElementKernel<scalar_t>);
                    break;
                case BinaryEWOpCode::Div:
                    LaunchBinaryEWKernel(indexer,
                                         CPUDivElementKernel<scalar_t>);
                    break;
                default:
                    break;
            }
        });
    }
}

}  // namespace kernel
}  // namespace core
}  // namespace open3d
