#include <stdbool.h>

#undef EXTERN

#ifdef DEFINE_QL_API
#define EXTERN
#else
#define EXTERN extern
#endif

#ifndef QLWS_API_H

#define QLWS_API_H

// loadQQLightAPI 
EXTERN int loadQQLightAPI(int* pErrorLine);

// QQLight API 
EXTERN const char* __stdcall (*QL_getPluginPath)(const char* authCode);
EXTERN void __stdcall (*QL_printLog)(const char* type, const char* text, int color, const char* authCode);
EXTERN void __stdcall (*QL_sendMessage)(int type, const char* group, const char* qq, const char* msg, const char* authCode);
EXTERN const char* __stdcall (*QL_withdrawMessage)(const char* group, const char* msgid, const char* authCode);
EXTERN const char* __stdcall (*QL_getFriendList)(int cache, const char* authCode);
EXTERN void __stdcall (*QL_addFriend)(const char* qq, const char* message, const char* authCode);
EXTERN void __stdcall (*QL_deleteFriend)(const char* qq, const char* authCode);
EXTERN const char* __stdcall (*QL_getGroupList)(int cache, const char* authCode);
EXTERN const char* __stdcall (*QL_getGroupMemberList)(const char* group, int cache, const char* authCode);
EXTERN void __stdcall (*QL_addGroup)(const char* group, const char* message, const char* authCode);
EXTERN void __stdcall (*QL_quitGroup)(const char* group, const char* authCode);
EXTERN const char* __stdcall (*QL_getGroupCard)(const char* group, const char* qq, const char* authCode);
EXTERN const char* __stdcall (*QL_uploadImage)(int type, const char* object, const char* data, const char* authCode);
EXTERN const char* __stdcall (*QL_getQQInfo)(const char* qq, const char* authCode);
EXTERN const char* __stdcall (*QL_getGroupInfo)(const char* group, const char* authCode);
EXTERN void __stdcall (*QL_inviteIntoGroup)(const char* group, const char* qq, const char* authCode);
EXTERN void __stdcall (*QL_setGroupCard)(const char* group, const char* qq, const char* name, const char* authCode);
EXTERN const char* __stdcall (*QL_getLoginAccount)(const char* authCode);
EXTERN void __stdcall (*QL_setSignature)(const char* content, const char* authCode);
EXTERN const char* __stdcall (*QL_getNickname)(const char* qq, const char* authCode);
EXTERN const char* __stdcall (*QL_getPraiseCount)(const char* qq, const char* authCode);
EXTERN void __stdcall (*QL_givePraise)(const char* qq, const char* authCode);
EXTERN void __stdcall (*QL_handleFriendRequest)(const char* qq, int type, const char* message, const char* authCode);
EXTERN void __stdcall (*QL_setState)(int type, const char* authCode);
EXTERN void __stdcall (*QL_handleGroupRequest)(const char* group, const char* qq, const char* seq, int type, const char* message, const char* authCode);
EXTERN void __stdcall (*QL_kickGroupMember)(const char* group, const char* qq, bool permanent, const char* authCode);
EXTERN void __stdcall (*QL_silence)(const char* group, const char* qq, int duration, const char* authCode);
EXTERN void __stdcall (*QL_globalSilence)(const char* group, bool enable, const char* authCode);
EXTERN const char* __stdcall (*QL_getCookies)(const char* authCode);
EXTERN const char* __stdcall (*QL_getBkn)(const char* cookies, const char* authCode);

#endif
