# Copyright (c) 2020 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if("ADUCM4X50" IN_LIST MBED_TARGET_LABELS)
    add_subdirectory(TARGET_ADUCM4X50)
elseif("ADUCM302X" IN_LIST MBED_TARGET_LABELS)
    add_subdirectory(TARGET_ADUCM302X)
endif()

target_include_directories(mbed-core
    INTERFACE
        .
)
