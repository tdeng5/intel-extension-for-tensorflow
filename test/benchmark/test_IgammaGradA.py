# Copyright (c) 2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================


"""Tests for IgammaGradA"""
import numpy as np
import tensorflow as tf
from tensorflow.python.framework import constant_op
from tensorflow.python.ops import gen_math_ops
from tensorflow.python.framework import dtypes
from utils import multi_run, add_profiling, flush_cache
from utils import common_2d_input_size

try:
    from intel_extension_for_tensorflow.python.test_func import test
except ImportError:
    from tensorflow.python.platform import test

try:
    from intel_extension_for_tensorflow.python.test_func import test
    FLOAT_COMPUTE_TYPE = [dtypes.float32, dtypes.float64] # FP16 and BF16 are not supported
except ImportError:
    from tensorflow.python.platform import test
    FLOAT_COMPUTE_TYPE = [dtypes.float32, dtypes.float64]
    
ITERATION = 5

class IgammaGradATest(test.TestCase):

    def _test_impl(self, size, dtype):
        np.random.seed(4)
        in_array_1 = np.random.normal(size=size)
        in_array_2 = np.random.normal(size=size)
        in_array_1 = constant_op.constant(in_array_1, dtype=dtype)
        in_array_2 = constant_op.constant(in_array_2, dtype=dtype)
        flush_cache()
        result = gen_math_ops.igamma_grad_a(in_array_1, in_array_2)

    @add_profiling
    @multi_run(ITERATION)
    def testIgammaGradA(self):
        for dtype in FLOAT_COMPUTE_TYPE:
            for in_size in common_2d_input_size:
                self._test_impl(in_size, dtype)


if __name__ == "__main__":
    test.main()
