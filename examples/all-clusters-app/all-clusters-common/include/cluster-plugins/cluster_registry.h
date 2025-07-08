#pragma once
#include <lib/core/DataModelTypes.h>
#include "cluster_handler.h"
#include <unordered_map>
#include <functional>
#include <memory>

// Factory マップを 1 箇所だけ実体化
std::unordered_map<chip::ClusterId,
                   std::function<std::unique_ptr<IClusterHandler>()>>&
ClusterFactoryMap();

IClusterHandler* GetHandler(chip::ClusterId);

// ---- 自己登録マクロ（行末に \ を置くこと！） ----
#define REGISTER_CLUSTER(cid, type)                      \
    namespace {                                          \
        struct _Reg_##type {                             \
            _Reg_##type() {                              \
                ClusterFactoryMap()[cid] = []() {        \
                    return std::make_unique<type>();     \
                };                                       \
            }                                            \
        };                                               \
        static _Reg_##type _auto_reg_##type;             \
    }
    