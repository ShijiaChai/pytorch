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

#ifndef CAFFE2_OPERATORS_TENSOR_PROTOS_DB_INPUT_H_
#define CAFFE2_OPERATORS_TENSOR_PROTOS_DB_INPUT_H_

#include <iostream>
#include <mutex>

#include "caffe2/core/db.h"
#include "caffe2/operators/prefetch_op.h"

namespace caffe2 {

template <class Context>
class TensorProtosDBInput final : public PrefetchOperator<Context> {
 public:
  using OperatorBase::OutputSize;
  using PrefetchOperator<Context>::prefetch_thread_;
  explicit TensorProtosDBInput(const OperatorDef& operator_def, Workspace* ws);
  ~TensorProtosDBInput() {
    PrefetchOperator<Context>::Finalize();
  }

  bool Prefetch() override;
  bool CopyPrefetched() override;

 private:
  // Prefetch will always just happen on the CPU side.
  vector<Blob> prefetched_blobs_;
  int batch_size_;
  bool shape_inferred_ = false;
  string key_;
  string value_;
};

template <class Context>
TensorProtosDBInput<Context>::TensorProtosDBInput(
    const OperatorDef& operator_def,
    Workspace* ws)
    : PrefetchOperator<Context>(operator_def, ws),
      prefetched_blobs_(operator_def.output_size()),
      batch_size_(
          OperatorBase::template GetSingleArgument<int>("batch_size", 0)) {}

template <class Context>
bool TensorProtosDBInput<Context>::Prefetch() {
  const db::DBReader& reader = OperatorBase::Input<db::DBReader>(0);
  TensorDeserializer<CPUContext> deserializer;
  if (batch_size_ == 0) {
    // We do not need to construct a batch. As a result, we will simply
    // deserialize everything into the target prefetched blob.
    reader.Read(&key_, &value_);
    TensorProtos protos;
    CAFFE_ENFORCE(protos.ParseFromString(value_));
    CAFFE_ENFORCE(protos.protos_size() == OutputSize());
    for (int i = 0; i < protos.protos_size(); ++i) {
      if (protos.protos(i).has_device_detail()) {
        protos.mutable_protos(i)->clear_device_detail();
      }
      deserializer.Deserialize(
          protos.protos(i),
          prefetched_blobs_[i].template GetMutable<TensorCPU>());
    }
  } else {
    vector<TensorCPU> temp_tensors(OutputSize());
    for (int item_id = 0; item_id < batch_size_; ++item_id) {
      reader.Read(&key_, &value_);
      TensorProtos protos;
      CAFFE_ENFORCE(protos.ParseFromString(value_));
      CAFFE_ENFORCE(protos.protos_size() == OutputSize());
      if (!shape_inferred_) {
        // First, set the shape of all the blobs.
        for (int i = 0; i < protos.protos_size(); ++i) {
          vector<int> dims(
              protos.protos(i).dims().begin(), protos.protos(i).dims().end());
          dims.insert(dims.begin(), batch_size_);
          prefetched_blobs_[i].template GetMutable<TensorCPU>()->Resize(dims);
        }
      }
      for (int i = 0; i < protos.protos_size(); ++i) {
        TensorCPU* dst = prefetched_blobs_[i].template GetMutable<TensorCPU>();
        TensorCPU& src = temp_tensors[i];
        if (protos.protos(i).has_device_detail()) {
          protos.mutable_protos(i)->clear_device_detail();
        }
        deserializer.Deserialize(protos.protos(i), &src);
        DCHECK_EQ(src.size() * batch_size_, dst->size());
        this->context_.template CopyItems<CPUContext, CPUContext>(
            src.meta(),
            src.size(),
            src.raw_data(),
            static_cast<char*>(dst->raw_mutable_data(src.meta())) +
                src.nbytes() * item_id);
      }
    }
  }
  return true;
}

template <class Context>
bool TensorProtosDBInput<Context>::CopyPrefetched() {
  for (int i = 0; i < OutputSize(); ++i) {
    OperatorBase::Output<Tensor<Context>>(i)->CopyFrom(
        prefetched_blobs_[i].template Get<TensorCPU>(), &this->context_);
  }
  return true;
}

} // namespace caffe2

#endif // CAFFE2_OPERATORS_TENSOR_PROTOS_DB_INPUT_H_
