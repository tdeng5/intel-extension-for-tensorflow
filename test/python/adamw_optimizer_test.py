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


"""Tests for adam optimizer with weight decay."""

import numpy as np
import keras
import tensorflow as tf
from intel_extension_for_tensorflow.python.ops import AdamWithWeightDecayOptimizer as itex_AdamW
from intel_extension_for_tensorflow.python.test_func import test
from intel_extension_for_tensorflow.python.test_func import test_util
import intel_extension_for_tensorflow as itex
from tensorflow.python.framework import dtypes
from tensorflow.python.framework import constant_op

DATA_TYPES = [
    dtypes.float32  # TODO(schen2): Currently itex AdamWithWeightDecayOptimizer only support float32.
]

WEIGHT_DECAY = 0.1

def adamw_update_numpy(
    param, grad_t, slot_vars, learning_rate, beta_1, beta_2, epsilon, weight_decay, amsgrad
):
    """Numpy update function for AdamW."""
    lr, beta1, beta2, eps, wd = (
        v() if callable(v) else v
        for v in (learning_rate, beta_1, beta_2, epsilon, weight_decay)
    )
    t = slot_vars.get("t", 0) + 1
    lr_t = lr * np.sqrt(1 - beta2 ** t) / (1 - beta1 ** t)
    slot_vars["m"] = beta1 * slot_vars.get("m", 0) + (1 - beta1) * grad_t
    slot_vars["v"] = beta2 * slot_vars.get("v", 0) + (1 - beta2) * grad_t ** 2
    if amsgrad:
        slot_vars["v_hat"] = slot_vars.get("v_hat", 0)
        slot_vars["v_hat"] = np.maximum(slot_vars["v_hat"], slot_vars["v"])
        param_t = param * (1 - wd * lr) - lr_t * slot_vars["m"] / (np.sqrt(slot_vars["v_hat"]) + eps)
    else:
        param_t = param * (1 - wd * lr) - lr_t * slot_vars["m"] / (np.sqrt(slot_vars["v"]) + eps)
    slot_vars["t"] = t
    return param_t, slot_vars

class AdamWeightDecayOptimizerTest(test_util.TensorFlowTestCase):

    def doTestBasic(self, use_callable_params=False, do_sparse=False, do_amsgrad=False):
        for dtype in DATA_TYPES:
            # Initialize variables for numpy implementation.
            np_slot_vars0, np_slot_vars1 = {}, {}
            var0_np = np.array([1.0, 2.0], dtype=dtype.as_numpy_dtype)
            grads0_np = np.array([0.1, 0.1], dtype=dtype.as_numpy_dtype)
            var1_np = np.array([3.0, 4.0], dtype=dtype.as_numpy_dtype)
            grads1_np = np.array([0.01, 0.01], dtype=dtype.as_numpy_dtype)

            # Create Tensorflow variables.
            itex_var0 = tf.Variable(var0_np)
            itex_var1 = tf.Variable(var1_np)

            # Adapt callable parameters
            learning_rate = lambda: 0.01
            beta_1=lambda: 0.9
            beta_2=lambda: 0.999
            if not use_callable_params:
                learning_rate = learning_rate()
                beta_1 = beta_1()
                beta_2 = beta_2()

            # Adapt sparse
            if do_sparse:
                grads0_np_indices = np.array([0, 1], dtype=np.int32)
                grads0 = tf.IndexedSlices(
                    tf.constant(grads0_np), tf.constant(grads0_np_indices), tf.constant([2])
                )
                grads1_np_indices = np.array([0, 1], dtype=np.int32)
                grads1 = tf.IndexedSlices(
                    tf.constant(grads1_np), tf.constant(grads1_np_indices), tf.constant([2])
                )
            else:
                grads0 = constant_op.constant(grads0_np)
                grads1 = constant_op.constant(grads1_np)

            itex_adamw_opt = itex_AdamW(weight_decay=WEIGHT_DECAY, learning_rate=learning_rate, amsgrad=do_amsgrad)
            # Run 3 steps of the optimizer
            for _ in range(3):
                itex_adamw_opt.apply_gradients(
                    zip([grads0, grads1], [itex_var0, itex_var1])
                )
                var0_np, np_slot_vars0 = adamw_update_numpy(
                    var0_np, grads0_np, np_slot_vars0, weight_decay=WEIGHT_DECAY, learning_rate=learning_rate, 
                    beta_1=beta_1, beta_2=beta_2, epsilon=1e-7, amsgrad=do_amsgrad)
                var1_np, np_slot_vars1 = adamw_update_numpy(
                    var1_np, grads1_np, np_slot_vars1, weight_decay=WEIGHT_DECAY, learning_rate=learning_rate, 
                    beta_1=beta_1, beta_2=beta_2, epsilon=1e-7, amsgrad=do_amsgrad)
                # Validate updated parameters
                self.assertAllCloseAccordingToType(itex_var0.numpy(), var0_np)
                self.assertAllCloseAccordingToType(itex_var1.numpy(), var1_np)

    def testBasicAdamW(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        self.doTestBasic()

    def testCallableParamsAdamW(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        self.doTestBasic(use_callable_params=True)

    def testAmsgradAdamW(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        self.doTestBasic(do_amsgrad=True)

    def testSparseAdamW(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        self.doTestBasic(do_sparse=True)
        self.doTestBasic(do_sparse=True, do_amsgrad=False)

    def testExcludeWeightDecayAdamW(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        grads, var1, var2, var3 = (
            tf.Variable(tf.zeros(())),
            tf.Variable(2.0),
            tf.Variable(2.0, name="exclude"),
            tf.Variable(2.0),
        )
        optimizer_1 = itex_AdamW(learning_rate=1.0, weight_decay=0.004)
        optimizer_1.apply_gradients(zip([grads], [var1]))

        optimizer_2 = itex_AdamW(learning_rate=1.0, weight_decay=0.004)
        optimizer_2.exclude_from_weight_decay(var_names=["exclude"])
        optimizer_2.apply_gradients(zip([grads, grads], [var1, var2]))

        optimizer_3 = itex_AdamW(learning_rate=1.0, weight_decay=0.004)
        optimizer_3.exclude_from_weight_decay(var_list=[var3])
        optimizer_3.apply_gradients(zip([grads, grads], [var1, var3]))

        self.assertAllCloseAccordingToType(var1.numpy(), 1.9760959)
        self.assertAllCloseAccordingToType(var2.numpy(), 2.0)
        self.assertAllCloseAccordingToType(var3.numpy(), 2.0)

    def testKerasFit(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        """Check if calling model.fit works."""
        model = tf.keras.models.Sequential([tf.keras.layers.Dense(2)])
        loss = tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True)
        adamw_opt = itex_AdamW(weight_decay=1e-4, learning_rate=1e-4)
        model.compile(optimizer=adamw_opt, loss=loss, metrics=["accuracy"])
        x, y = np.random.uniform(size=(2, 4, 1))
        model.fit(x, y, epochs=1)

    def test_clip_norm(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        for dtype in DATA_TYPES:
            optimizer = itex_AdamW(clipnorm=1)
            grad = [np.array([100.0, 100.0], dtype=dtype.as_numpy_dtype)]
            clipped_grad = optimizer._clip_gradients(grad)
            self.assertAllClose(clipped_grad[0], [2**0.5 / 2, 2**0.5 / 2])

    def test_clip_value(self):
        '''ResourceApplyAdamWithWeightDecay is a DPCPP op, don't have cpu registration 
            TODO: waiting for CPU registration of ResourceApplyAdamWithWeightDecay then enable
            this test case on CPU'''
        if not test.is_gpu_available():
            self.skipTest("No GPU available")
        for dtype in DATA_TYPES:    
            optimizer = itex_AdamW(clipvalue=1)
            grad = [np.array([100.0, 100.0], dtype=dtype.as_numpy_dtype)]
            clipped_grad = optimizer._clip_gradients(grad)
            self.assertAllClose(clipped_grad[0], [1.0, 1.0])

if __name__ == "__main__":
    test.main()
