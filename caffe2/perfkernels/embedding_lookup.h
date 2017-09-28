/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "caffe2/core/common.h"

namespace caffe2 {

/**
 * Embedding lookup with reduction.
 *
 * `input` of size data_size * block_size
 * `indices` of size index_size
 * `lengths` of size output_size
 * `weights` nullptr or array of size index_size
 * `out` of size output_size * block_size
 * sum(lengths[i]) == index_size
 *
 * Behavior is roughly equivalent to pseudocode:
 *
 * pos = 0
 * for (i = 0..index_size-1)
 *   for (k = 0..block_size-1)
 *     out[i*block_size + k] = 0
 *   for (j = 0..lengths[i]-1)
 *     for (k = 0..block_size-1)
 *       out[i*block_size + k] += input[indices[pos]*block_size + k] *
 *                                (weights ? weights[pos] : 1.0)
 *     pos += 1
 *   if (normalize_weights && lengths[i] > 0)
 *     for (k = 0..block_size-1)
 *       out[i*block_size + k] /= lengths[i]
 *
 */
template <typename IndexType, typename InType, typename OutType>
void EmbeddingLookup(
    const TIndex block_size,
    const TIndex output_size,
    const TIndex index_size,
    const TIndex data_size,
    const InType* input,
    const IndexType* indices,
    const int* lengths,
    const float* weights, // optional, can be null for non-weighted sum
    const float* scale_bias, // optional scale & bias params for uint8 input
    bool normalize_by_lengths,
    OutType* out);

} // namespace caffe2
