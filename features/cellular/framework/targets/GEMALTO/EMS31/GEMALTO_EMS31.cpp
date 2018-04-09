/*
 * Copyright (c) 2017, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
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

#include "GEMALTO_CINTERION_CellularNetwork.h"
#include "GEMALTO_CINTERION_CellularStack.h"
#include "GEMALTO_EMS31.h"

using namespace mbed;
using namespace events;

GEMALTO_EMS31::GEMALTO_EMS31(EventQueue &queue) : GEMALTO_CINTERION(queue)
{
}

GEMALTO_EMS31::~GEMALTO_EMS31()
{
}

CellularNetwork *GEMALTO_EMS31::open_network(FileHandle *fh)
{
    if (!_network) {
        _network = new GEMALTO_CINTERION_CellularNetwork(*get_at_handler(fh));
    }
    return _network;
}
