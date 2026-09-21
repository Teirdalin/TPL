// SPDX-License-Identifier: LicenseRef-JDL-1
// Copyright (c) 2026 Teirdalin. See PLUGIN_API_PERMISSION.md for API reuse rights.
#ifndef TPLLIB_UI_H
#define TPLLIB_UI_H
#include "tpllib.h"
#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push,8)
#define TPLLIB_UI_SERVICE "tpl.ui"
#define TPLLIB_UI_VERSION 1u
#define TPLLIB_UI_BUTTON 1u
#define TPLLIB_UI_LIST 2u
#define TPLLIB_UI_VISIBLE 1u
#define TPLLIB_UI_ENABLED 2u
#define TPLLIB_UI_KEY_HANDLER 4u
#define TPLLIB_UI_DELETE_KEY 211u
typedef struct TPLLib_UI_Rect { int32_t left,top,width,height; } TPLLib_UI_Rect;
typedef struct TPLLib_UI_Info {
    uint32_t size,kind,flags,reserved;
    TPLLib_UI_Rect rect;
    TPLLib_Token parent;
    uint64_t selected,count;
} TPLLib_UI_Info;
typedef void (*TPLLib_UI_Click)(void* user,TPLLib_Token sender);
/* All operations require the GUI thread. Sessions own additions and reversible
   edits; widget handles expire on native destruction and are never recycled.
   Native key dispatch is allowed only during a real owned-button click, once,
   to a visible/enabled MultiListBox in the same root. The reviewed Kenshi
   GamesList route may dispatch through its two native ListBox columns. Version
   1 supports only TPLLIB_UI_DELETE_KEY. No process-wide key injection. */
typedef struct TPLLib_UI_API {
    uint32_t size,version;
    TPLLib_Status (*session_open)(const char* name,TPLLib_Token* session);
    TPLLib_Status (*session_close)(TPLLib_Token session);
    TPLLib_Status (*find)(TPLLib_Token parent,const char* name_suffix,TPLLib_Token* widget);
    TPLLib_Status (*info)(TPLLib_Token widget,TPLLib_UI_Info* info);
    TPLLib_Status (*button_create)(TPLLib_Token session,TPLLib_Token parent,TPLLib_Token style,
        const TPLLib_UI_Rect* rect,const char* caption,TPLLib_UI_Click click,void* user,TPLLib_Token* widget);
    TPLLib_Status (*button_set)(TPLLib_Token session,TPLLib_Token widget,
        const TPLLib_UI_Rect* rect,const char* caption,int enabled);
    TPLLib_Status (*key_event)(TPLLib_Token session,TPLLib_Token widget,uint32_t key);
} TPLLib_UI_API;
#pragma pack(pop)
#ifdef __cplusplus
}
#endif
#endif
