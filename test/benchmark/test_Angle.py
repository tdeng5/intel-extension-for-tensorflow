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


import numpy as np
import tensorflow as tf
from tensorflow.python.framework import dtypes
from tensorflow.python.ops import math_ops
from tensorflow.python.framework import constant_op
from utils import multi_run, add_profiling, flush_cache

try:
    from intel_extension_for_tensorflow.python.test_func import test
except ImportError:
    from tensorflow.python.platform import test

COMPUTE_TYPE = [dtypes.complex64]
ITERATION = 5

class AngleTest(test.TestCase):
    def _test_impl(self, size, dtype):
        in_array = np.random.normal(size=[size])
        in_array = constant_op.constant(in_array, dtype=dtype)
        flush_cache()
        out_gpu = math_ops.angle(in_array)

    @add_profiling
    @multi_run(ITERATION)
    def testAngle(self):
        for dtype in COMPUTE_TYPE:
            self._test_impl(30523, dtype)
            
if __name__ == '__main__':
    test.main()    
