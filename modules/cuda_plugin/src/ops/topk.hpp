// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cudnn_ops_infer.h>

#include <cuda/device_pointers.hpp>
#include <cuda_operation_base.hpp>
#include <kernels/topk.hpp>
#include <ngraph/op/topk.hpp>
#include <ngraph/shape.hpp>
#include <ngraph/type/element_type.hpp>

namespace CUDAPlugin {

class TopKOp : public OperationBase {
public:
    explicit TopKOp(const CreationContext& context,
                    const ngraph::Node& node,
                    IndexCollection&& inputIds,
                    IndexCollection&& outputIds);
    void Execute(const InferenceRequestContext& context,
                 Inputs inputTensors,
                 Outputs outputTensors,
                 const Workbuffers& workbuffers) const override;

    void InitSharedImmutableWorkbuffers(const Buffers&) override;
    WorkbufferRequest GetWorkBufferRequest() const override;

private:
    size_t workspace_size_;
    kernel::TopK::KernelParam kernel_param_;
    std::optional<kernel::TopK> kernel_;
};

}  // namespace CUDAPlugin
