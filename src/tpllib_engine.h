// SPDX-License-Identifier: LicenseRef-JDL-1
#ifndef TPLLIB_ENGINE_INTERNAL_H
#define TPLLIB_ENGINE_INTERNAL_H
#include "../include/tpllib_engine.h"
namespace tplengine {
TPLLib_Status initialize(const TPLLib_API* host);
const TPLLib_Engine_API* api();
}
#endif
