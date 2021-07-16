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

#include "open3d/core/nns/FixedRadiusIndex.h"

#ifdef BUILD_CUDA_MODULE
#include "open3d/core/nns/FixedRadiusSearch.h"
#endif

#include "open3d/core/Dispatch.h"
#include "open3d/utility/Logging.h"

namespace open3d {
namespace core {
namespace nns {

FixedRadiusIndex::FixedRadiusIndex(){};

FixedRadiusIndex::FixedRadiusIndex(const Tensor &dataset_points,
                                   double radius) {
    SetTensorData(dataset_points, radius);
};

FixedRadiusIndex::~FixedRadiusIndex(){};

bool FixedRadiusIndex::SetTensorData(const Tensor &dataset_points,
                                     double radius) {
#ifdef BUILD_CUDA_MODULE
    if (dataset_points.GetDevice().GetType() != Device::DeviceType::CUDA) {
        utility::LogError(
                "[FixedRadiusIndex::SetTensorData] dataset_points should be "
                "GPU Tensor.");
    }
    if (radius <= 0) {
        utility::LogError(
                "[FixedRadiusIndex::SetTensorData] radius should be positive.");
    }
    dataset_points_ = dataset_points.Contiguous();
    Device device = GetDevice();
    Dtype dtype = GetDtype();

    int64_t num_dataset_points = GetDatasetSize();
    int64_t hash_table_size = std::min<int64_t>(
            std::max<int64_t>(hash_table_size_factor * num_dataset_points, 1),
            max_hash_tabls_size);
    points_row_splits_ = std::vector<int64_t>({0, num_dataset_points});
    hash_table_splits_ = std::vector<int64_t>({0, hash_table_size});

    hash_table_index_ =
            Tensor::Empty({num_dataset_points}, Dtype::Int64, device);
    hash_table_cell_splits_ = Tensor::Empty({hash_table_splits_.back() + 1},
                                            Dtype::Int64, device);

    void *temp_ptr = nullptr;
    size_t temp_size = 0;

    DISPATCH_FLOAT_DTYPE_TO_TEMPLATE(dtype, [&]() {
        // Determine temp_size.
        BuildSpatialHashTableCUDA(temp_ptr, temp_size, num_dataset_points,
                                  dataset_points_.GetDataPtr<scalar_t>(),
                                  scalar_t(radius), points_row_splits_.size(),
                                  points_row_splits_.data(),
                                  hash_table_splits_.data(),
                                  hash_table_cell_splits_.GetShape()[0],
                                  hash_table_cell_splits_.GetDataPtr<int64_t>(),
                                  hash_table_index_.GetDataPtr<int64_t>());
        Tensor temp_tensor =
                Tensor::Empty({int64_t(temp_size)}, Dtype::UInt8, device);
        temp_ptr = temp_tensor.GetDataPtr();

        // Actually run the function.
        BuildSpatialHashTableCUDA(temp_ptr, temp_size, num_dataset_points,
                                  dataset_points_.GetDataPtr<scalar_t>(),
                                  scalar_t(radius), points_row_splits_.size(),
                                  points_row_splits_.data(),
                                  hash_table_splits_.data(),
                                  hash_table_cell_splits_.GetShape()[0],
                                  hash_table_cell_splits_.GetDataPtr<int64_t>(),
                                  hash_table_index_.GetDataPtr<int64_t>());
    });
    return true;
#else
    utility::LogError(
            "FixedRadiusIndex::SetTensorData BUILD_CUDA_MODULE is OFF. Please "
            "compile Open3d with BUILD_CUDA_MODULE=ON.");
#endif
};

std::tuple<Tensor, Tensor, Tensor> FixedRadiusIndex::SearchRadius(
        const Tensor &query_points, double radius, bool sort) const {
#ifdef BUILD_CUDA_MODULE
    Dtype dtype = GetDtype();
    Device device = GetDevice();
    int64_t num_dataset_points = GetDatasetSize();

    // Check dtype.
    query_points.AssertDtype(dtype);

    // Check shape.
    query_points.AssertShapeCompatible({utility::nullopt, GetDimension()});

    // Check device.
    query_points.AssertDevice(device);

    if (radius <= 0) {
        utility::LogError(
                "[FixedRadiusIndex::SearchRadius] radius should be positive.");
    }

    Tensor query_points_ = query_points.Contiguous();
    int64_t num_query_points = query_points_.GetShape()[0];
    std::vector<int64_t> queries_row_splits({0, num_query_points});

    void *temp_ptr = nullptr;
    size_t temp_size = 0;

    Tensor neighbors_index;
    Tensor neighbors_distance;
    Tensor neighbors_row_splits =
            Tensor({num_query_points + 1}, Dtype::Int64, device);

    DISPATCH_FLOAT_DTYPE_TO_TEMPLATE(dtype, [&]() {
        NeighborSearchAllocator<scalar_t> output_allocator(device);
        // Determine temp_size.
        FixedRadiusSearchCUDA(
                temp_ptr, temp_size, neighbors_row_splits.GetDataPtr<int64_t>(),
                num_dataset_points, dataset_points_.GetDataPtr<scalar_t>(),
                num_query_points, query_points_.GetDataPtr<scalar_t>(),
                scalar_t(radius), points_row_splits_.size(),
                points_row_splits_.data(), queries_row_splits.size(),
                queries_row_splits.data(), hash_table_splits_.data(),
                hash_table_cell_splits_.GetShape()[0],
                hash_table_cell_splits_.GetDataPtr<int64_t>(),
                hash_table_index_.GetDataPtr<int64_t>(), output_allocator);

        Tensor temp_tensor =
                Tensor::Empty({int64_t(temp_size)}, Dtype::UInt8, device);
        temp_ptr = temp_tensor.GetDataPtr();

        // Actually run the function.
        FixedRadiusSearchCUDA(
                temp_ptr, temp_size, neighbors_row_splits.GetDataPtr<int64_t>(),
                num_dataset_points, dataset_points_.GetDataPtr<scalar_t>(),
                num_query_points, query_points_.GetDataPtr<scalar_t>(),
                scalar_t(radius), points_row_splits_.size(),
                points_row_splits_.data(), queries_row_splits.size(),
                queries_row_splits.data(), hash_table_splits_.data(),
                hash_table_cell_splits_.GetShape()[0],
                hash_table_cell_splits_.GetDataPtr<int64_t>(),
                hash_table_index_.GetDataPtr<int64_t>(), output_allocator);

        Tensor indices_unsorted = output_allocator.NeighborsIndex();
        Tensor distances_unsorted = output_allocator.NeighborsDistance();

        if (!sort) {
            neighbors_index = indices_unsorted;
            neighbors_distance = distances_unsorted;
        } else {
            // Sort indices & distances.
            temp_ptr = nullptr;
            temp_size = 0;

            int64_t num_indices = indices_unsorted.GetShape()[0];
            int64_t num_segments = neighbors_row_splits.GetShape()[0] - 1;
            Tensor indices_sorted =
                    Tensor::Empty({num_indices}, Dtype::Int64, device);
            Tensor distances_sorted =
                    Tensor::Empty({num_indices}, dtype, device);

            // Determine temp_size for sorting
            SortPairs(temp_ptr, temp_size, num_indices, num_segments,
                      neighbors_row_splits.GetDataPtr<int64_t>(),
                      indices_unsorted.GetDataPtr<int64_t>(),
                      distances_unsorted.GetDataPtr<scalar_t>(),
                      indices_sorted.GetDataPtr<int64_t>(),
                      distances_sorted.GetDataPtr<scalar_t>());

            temp_tensor =
                    Tensor::Empty({int64_t(temp_size)}, Dtype::UInt8, device);
            temp_ptr = temp_tensor.GetDataPtr();

            // Actually run the sorting.
            SortPairs(temp_ptr, temp_size, num_indices, num_segments,
                      neighbors_row_splits.GetDataPtr<int64_t>(),
                      indices_unsorted.GetDataPtr<int64_t>(),
                      distances_unsorted.GetDataPtr<scalar_t>(),
                      indices_sorted.GetDataPtr<int64_t>(),
                      distances_sorted.GetDataPtr<scalar_t>());
            neighbors_index = indices_sorted;
            neighbors_distance = distances_sorted;
        }
    });
    return std::make_tuple(neighbors_index, neighbors_distance,
                           neighbors_row_splits);
#else
    utility::LogError(
            "FixedRadiusIndex::SearchRadius BUILD_CUDA_MODULE is OFF. Please "
            "compile Open3d with BUILD_CUDA_MODULE=ON.");
#endif
};

std::tuple<Tensor, Tensor, Tensor> FixedRadiusIndex::SearchHybrid(
        const Tensor &query_points, double radius, int max_knn) const {
#ifdef BUILD_CUDA_MODULE
    Dtype dtype = GetDtype();
    Device device = GetDevice();
    int64_t num_dataset_points = GetDatasetSize();

    // Check dtype.
    query_points.AssertDtype(dtype);

    // Check shape.
    query_points.AssertShapeCompatible({utility::nullopt, GetDimension()});

    // Check device.
    query_points.AssertDevice(device);

    if (radius <= 0) {
        utility::LogError(
                "[FixedRadiusIndex::SearchRadius] radius should be positive.");
    }

    Tensor query_points_ = query_points.Contiguous();
    int64_t num_query_points = query_points_.GetShape()[0];
    std::vector<int64_t> queries_row_splits({0, num_query_points});

    Tensor neighbors_index, neighbors_distance, neighbour_counts;

    DISPATCH_FLOAT_DTYPE_TO_TEMPLATE(dtype, [&]() {
        NeighborSearchAllocator<scalar_t> output_allocator(device);
        // Determine temp_size.
        HybridSearchCUDA(
                num_dataset_points, dataset_points_.GetDataPtr<scalar_t>(),
                num_query_points, query_points_.GetDataPtr<scalar_t>(),
                scalar_t(radius), max_knn, points_row_splits_.size(),
                points_row_splits_.data(), queries_row_splits.size(),
                queries_row_splits.data(), hash_table_splits_.data(),
                hash_table_cell_splits_.GetShape()[0],
                hash_table_cell_splits_.GetDataPtr<int64_t>(),
                hash_table_index_.GetDataPtr<int64_t>(), output_allocator);

        neighbors_index = output_allocator.NeighborsIndex();
        neighbors_distance = output_allocator.NeighborsDistance();
        neighbour_counts = output_allocator.NeighborCounts();
    });
    return std::make_tuple(neighbors_index.View({num_query_points, max_knn}),
                           neighbors_distance.View({num_query_points, max_knn}),
                           neighbour_counts.View({num_query_points}));
#else
    utility::LogError(
            "FixedRadiusIndex::SearchHybrid BUILD_CUDA_MODULE is OFF. Please "
            "compile Open3d with BUILD_CUDA_MODULE=ON.");
#endif
}
}  // namespace nns
}  // namespace core
}  // namespace open3d
