#!/usr/bin/env bash
#
# Copyright (c) 2021-2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e
IMAGE_TYPE=$1
if [ $IMAGE_TYPE == "xpu" -o $IMAGE_TYPE == "gpu" ]
then
        IMAGE_NAME=intel-extension-for-tensorflow:$IMAGE_TYPE
        docker build --no-cache --build-arg UBUNTU_VERSION=22.04 \
                                --build-arg ICD_VER=23.30.26918.50-736~22.04 \
                                --build-arg LEVEL_ZERO_GPU_VER=1.3.26918.50-736~22.04 \
                                --build-arg LEVEL_ZERO_VER=1.13.1-719~22.04 \
                                --build-arg LEVEL_ZERO_DEV_VER=1.13.1-719~22.04 \
                                --build-arg DPCPP_VER=2024.0.0-49819 \
                                --build-arg MKL_VER=2024.0.0-49656 \
                                --build-arg CCL_VER=2021.11.0-49156 \
                                --build-arg PYTHON=python3.10 \
                                --build-arg TF_VER=2.14 \
                                --build-arg WHEELS=*.whl \
                                -t $IMAGE_NAME \
				-f itex-xpu.Dockerfile .
else
        IMAGE_NAME=intel-extension-for-tensorflow:$IMAGE_TYPE
        docker build --no-cache --build-arg UBUNTU_VERSION=22.04 \
                                --build-arg PYTHON=python3.10 \
                                --build-arg TF_VER=2.14 \
                                --build-arg WHEELS=*.whl \
                                -t $IMAGE_NAME \
                                -f itex-cpu.Dockerfile .
fi

