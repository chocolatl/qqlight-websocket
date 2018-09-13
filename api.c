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

	if(
		(QL_getPluginPath = GetProcAddress(libHandle, "Api_GetPath")) == NULL ||
		(QL_printLog = GetProcAddress(libHandle, "Api_SendLog")) == NULL ||
		(QL_sendMessage = GetProcAddress(libHandle, "Api_SendMsg")) == NULL ||
		(QL_withdrawMessage = GetProcAddress(libHandle, "Api_DeleteMsg")) == NULL ||
		(QL_getFriendList = GetProcAddress(libHandle, "Api_GetFriendList")) == NULL ||
		(QL_addFriend = GetProcAddress(libHandle, "Api_AddFriend")) == NULL ||
		(QL_deleteFriend = GetProcAddress(libHandle, "Api_DeleteFriend")) == NULL ||
		(QL_getGroupList = GetProcAddress(libHandle, "Api_GetGroupList")) == NULL ||
		(QL_getGroupMemberList = GetProcAddress(libHandle, "Api_GetGroupMemberList")) == NULL ||
		(QL_addGroup = GetProcAddress(libHandle, "Api_AddGroup")) == NULL ||
		(QL_quitGroup = GetProcAddress(libHandle, "Api_QuitGroup")) == NULL ||
		(QL_getGroupCard = GetProcAddress(libHandle, "Api_GetGroupCard")) == NULL
	) {
		FreeLibrary(libHandle);
		return -1;
	}

	isLoaded = true;
	
	return 0;
}
