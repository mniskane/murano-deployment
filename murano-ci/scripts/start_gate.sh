#!/bin/bash
# Copyright (c) 2015 Mirantis, Inc.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#

CI_ROOT_DIR=$(cd $(dirname "$0") && cd .. && pwd)

# Include of the common functions library file
source "${CI_ROOT_DIR}/scripts/common.inc"
#-----------------

source "${CI_ROOT_DIR}/scripts/deploy_devstack.sh"

pushd "${STACK_HOME}/devstack"
set +o xtrace
echo "Importing openrc ..."
source openrc "${ADMIN_USERNAME}" "${ADMIN_TENANT}"
set -o xtrace
popd

source "${CI_ROOT_DIR}/scripts/prepare_tests.sh"
source "${CI_ROOT_DIR}/scripts/run_tests.sh"
