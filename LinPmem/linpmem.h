// Copyright 2018 Velocidex Innovations <mike@velocidex.com>
// Copyright 2014 - 2017 Google Inc.
// Copyright 2012 Google Inc. All Rights Reserved.
// Author: Viviane Zwanger,
// derived from Rekall/WinPmem by Mike Cohen and Johannes Stüttgen.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _LINPMEM_H_
#define _LINPMEM_H_

#include "userspace_interface/linpmem_shared.h"

#include "pte_mmap.h"
#include "pte_mmap.c"

#define LINPMEM_DRIVER_VERSION "0.9.0"

MODULE_LICENSE("Apache-2.0");
MODULE_AUTHOR("Viviane Zwanger/Mike Cohen");
MODULE_DESCRIPTION("Updated Pmem driver for Linux");


/*
  Our Device Extension Structure.
*/
typedef struct _DEVICE_EXTENSION
{
    PTE_METHOD_DATA  pte_data; // our management data. Contains volatile PPTE rogue_pte.
    // Currently no other data used.
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// ...and a global instance of that:
static DEVICE_EXTENSION g_DeviceExtension = {0};
// Aside from the sacrifice custom section, this is the only global data.

#endif
