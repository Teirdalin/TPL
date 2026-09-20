// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin. See PLUGIN_API_PERMISSION.md for API reuse rights.
#ifndef TPLLIB_ENGINE_SAVE_HPP
#define TPLLIB_ENGINE_SAVE_HPP
#include "tpllib_engine.h"
#include <string>
// Native x64 VC100 /MD ABI only. Incomplete types deliberately prohibit
// allocating game-owned objects using unverified class layouts.
namespace MyGUI { class Widget; class Window; struct KeyCode;
    namespace delegates { template<typename T> class IDelegate1; } }
namespace TPLLib {
struct LoadSaveWindow;
struct ForgottenGUI;
typedef LoadSaveWindow* (*LoadSaveConstructFn)(LoadSaveWindow*,const std::string&);
typedef void (*LoadSaveCloseFn)(LoadSaveWindow*,MyGUI::Widget*);
typedef void (*LoadSaveKeyFn)(LoadSaveWindow*,MyGUI::Widget*,MyGUI::KeyCode,unsigned int);
typedef MyGUI::Window* (*MessageBoxFn)(ForgottenGUI*,const std::string&,const std::string&,
    int,bool,MyGUI::delegates::IDelegate1<int>*);
}
#endif
