/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once
#include <app/OperationalSessionSetup.h>
#include <app/util/basic-types.h>
#include "lib/core/CHIPError.h"

CHIP_ERROR InitBindingHandlers();
void       SwitchOnOffAttributeUpdated(chip::EndpointId ep, bool value);

// 前方宣言（cpp 側でのみ必要な型）
namespace chip { class OperationalDeviceProxy; }
struct EmberBindingTableEntry;
