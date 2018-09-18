#include <stdbool.h>

#ifndef QLWS_API_H

#define QLWS_API_H

// loadQQLightAPI 
int loadQQLightAPI(void);

// QQLight API 
const char* __stdcall (*QL_getPluginPath)(const char* authCode);
void __stdcall (*QL_printLog)(const char* type, const char* text, int color, const char* authCode);
void __stdcall (*QL_sendMessage)(int type, const char* group, const char* qq, const char* msg, const char* authCode);
const char* __stdcall (*QL_withdrawMessage)(const char* group, const char* msgid, const char* authCode);
const char* __stdcall (*QL_getFriendList)(const char* authCode);
void __stdcall (*QL_addFriend)(const char* qq, const char* message, const char* authCode);
void __stdcall (*QL_deleteFriend)(const char* qq, const char* authCode);
const char* __stdcall (*QL_getGroupList)(const char* authCode);
const char* __stdcall (*QL_getGroupMemberList)(const char* group, const char* authCode);
void __stdcall (*QL_addGroup)(const char* group, const char* message, const char* authCode);
void __stdcall (*QL_quitGroup)(const char* group, const char* authCode);
const char* __stdcall (*QL_getGroupCard)(const char* group, const char* qq, const char* authCode);
const char* __stdcall (*QL_uploadImage)(int type, const char* object, const char* data, const char* authCode);
const char* __stdcall (*QL_getQQInfo)(const char* qq, const char* authCode);
const char* __stdcall (*QL_getGroupInfo)(const char* group, const char* authCode);
void __stdcall (*QL_inviteIntoGroup)(const char* group, const char* qq, const char* authCode);
void __stdcall (*QL_setGroupCard)(const char* group, const char* qq, const char* name, const char* authCode);
const char* __stdcall (*QL_getLoginAccount)(const char* authCode);
void __stdcall (*QL_setSignature)(const char* content, const char* authCode);
const char* __stdcall (*QL_getNickname)(const char* qq, const char* authCode);
const char* __stdcall (*QL_getPraiseCount)(const char* qq, const char* authCode);
void __stdcall (*QL_givePraise)(const char* qq, const char* authCode);
void __stdcall (*QL_handleFriendRequest)(const char* qq, int type, const char* message, const char* authCode);
void __stdcall (*QL_setState)(int type, const char* authCode);

#endif
