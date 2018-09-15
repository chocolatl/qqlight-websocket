#include <windows.h>
#include <stdbool.h>
#include "api.h"

bool isLoaded = false;
HMODULE libHandle;

int loadQQLightAPI(void) {

    // 一个玄学问题，不加Sleep(1)，只有一个插件时，插件刷新时会崩溃
    Sleep(1);

    if(isLoaded) return 0;
    
    if((libHandle = LoadLibrary("message.dll")) == NULL) {
        return -1;
    }

    #define GET_DLL_FUNC(dname, ename) if((dname = GetProcAddress(libHandle, ename)) == NULL) goto laodFuncError;

    GET_DLL_FUNC(QL_getPluginPath, "Api_GetPath");
    GET_DLL_FUNC(QL_printLog, "Api_SendLog");
    GET_DLL_FUNC(QL_sendMessage, "Api_SendMsg");
    GET_DLL_FUNC(QL_withdrawMessage, "Api_DeleteMsg");
    GET_DLL_FUNC(QL_getFriendList, "Api_GetFriendList");
    GET_DLL_FUNC(QL_addFriend, "Api_AddFriend");
    GET_DLL_FUNC(QL_deleteFriend, "Api_DeleteFriend");
    GET_DLL_FUNC(QL_getGroupList, "Api_GetGroupList");
    GET_DLL_FUNC(QL_getGroupMemberList, "Api_GetGroupMemberList");
    GET_DLL_FUNC(QL_addGroup, "Api_AddGroup");
    GET_DLL_FUNC(QL_quitGroup, "Api_QuitGroup");
    GET_DLL_FUNC(QL_getGroupCard, "Api_GetGroupCard");
    GET_DLL_FUNC(QL_uploadImage, "Api_UpLoadPic");

    isLoaded = true;
    return 0;

    laodFuncError:
    FreeLibrary(libHandle);
    return -1;
}
