# Copyright (c) 2021 Intel Corporation
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

"""device"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from intel_extension_for_tensorflow.python._pywrap_itex import *
from intel_extension_for_tensorflow.core.utils.protobuf import config_pb2

def get_backend():
  return ITEX_GetBackend()

def is_xehpc():
  isxehpc = ITEX_IsXeHPC()
  isxehpc_proto = config_pb2.ConfigProto()
  isxehpc_proto.ParseFromString(isxehpc)  
  return isxehpc_proto.graph_options.device_isxehpc

def has_xmx():
  hasxmx = ITEX_HasXMX()
  hasxmx_proto = config_pb2.ConfigProto()
  hasxmx_proto.ParseFromString(hasxmx)
  return hasxmx_proto.graph_options.device_hasxmx
