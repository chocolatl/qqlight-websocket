#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <winsock2.h>
#include <unistd.h>
#include "lib/cjson/cJSON.h"
#include "api.h"
#include "ws.h"
#include "server.h"

#define DllExport(returnType) __declspec(dllexport) returnType __stdcall

const char* PLUGIN_INFO = 
    "pluginID=websocket.protocol;\r\n"
    "pluginName=WebSocket Protocol;\r\n"
    "pluginBrief="
        "Enable you to use QQLight API in any language you like via WebSocket.\r\n\r\n"
        "GitHub:\r\nhttps://github.com/Chocolatl/qqlight-websocket;\r\n"
    "pluginVersion=0.5.0;\r\n"
    "pluginSDK=3;\r\n"
    "pluginAuthor=Chocolatl;\r\n"
    "pluginWindowsTitle=;"
;

char authCode[64];
char pluginPath[1024];

struct {
    u_short port;
    char path[256];
} config = {
    port: 49632,
    path: "/"
};

void pluginLog(const char* type, const char* format, ...) {
	char buff[512];
	va_list arg;

	va_start(arg, format);
	vsnprintf(buff, sizeof(buff) - 1, format, arg);
	va_end(arg);

	QL_printLog(type, buff, 0, authCode);
}

// 返回转换后数据地址，记得free
char* GBKToUTF8(const char* str) {
    
    // GB18030代码页
    const int CODE_PAGE = 54936;

    int n = MultiByteToWideChar(CODE_PAGE, 0, str, -1, NULL, 0);
    wchar_t u16str[n + 1];
    MultiByteToWideChar(CODE_PAGE, 0, str, -1, u16str, n);

    n = WideCharToMultiByte(CP_UTF8, 0, u16str, -1, NULL, 0, NULL, NULL);
    char* u8str = malloc(n + 1);
    WideCharToMultiByte(CP_UTF8, 0, u16str, -1, u8str, n, NULL, NULL);

    return u8str;
}

// 返回转换后数据地址，记得free
char* UTF8ToGBK(const char* str) {

    // GB18030代码页
    const int CODE_PAGE = 54936;

    int n = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    wchar_t u16str[n + 1];
    MultiByteToWideChar(CP_UTF8, 0, str, -1, u16str, n);

    n = WideCharToMultiByte(CODE_PAGE, 0, u16str, -1, NULL, 0, NULL, NULL);
    char* gbstr = malloc(n + 1);
    WideCharToMultiByte(CODE_PAGE,0, u16str, -1, gbstr, n, NULL, NULL);

    return gbstr;
}

void wsClientTextDataHandle(const char* payload, uint64_t payloadLen, SOCKET socket) {
    
    // 注意，payload的文本数据不是以\0结尾
    pluginLog("wsClientDataHandle","Payload data is %.*s", payloadLen > 128 ? 128 : (unsigned int)payloadLen, payload);

    const char* parseEnd;

    cJSON *json = cJSON_ParseWithOpts(payload, &parseEnd, 0);

    if(json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            pluginLog("jsonParse", "Error before: %d", error_ptr - payload);
        }
        return;
    }

    // 公有字段
    const cJSON* j_id     = cJSON_GetObjectItemCaseSensitive(json, "id");        // cJSON_GetObjectItemCaseSensitive获取不存在的字段时会返回NULL
    const cJSON* j_method = cJSON_GetObjectItemCaseSensitive(json, "method");
    const cJSON* j_params = cJSON_GetObjectItemCaseSensitive(json, "params");

    const cJSON_bool e_id     = cJSON_IsString(j_id);        // 如果j_xx的值为NULL的时候也会返回FALSE，所以e_xx为TRUE时可以保证字段存在且类型正确
    const cJSON_bool e_method = cJSON_IsString(j_method);
    const cJSON_bool e_params = cJSON_IsString(j_params);

    const char* v_id     = e_id     ?  j_id->valuestring      : NULL;
    const char* v_method = e_method ?  j_method->valuestring  : NULL;
    const char* v_params = e_params ?  j_params->valuestring  : NULL;

    if(!e_method) {
        cJSON_Delete(json);
        return;
    }

    // 参数字段
    const cJSON* j_type     = cJSON_GetObjectItemCaseSensitive(j_params, "type");        // 即使j_params为NULL也是安全的，返回的结果也是NULL
    const cJSON* j_group    = cJSON_GetObjectItemCaseSensitive(j_params, "group");
    const cJSON* j_qq       = cJSON_GetObjectItemCaseSensitive(j_params, "qq");
    const cJSON* j_content  = cJSON_GetObjectItemCaseSensitive(j_params, "content");
    const cJSON* j_msgid    = cJSON_GetObjectItemCaseSensitive(j_params, "msgid");
    const cJSON* j_message  = cJSON_GetObjectItemCaseSensitive(j_params, "message");
    const cJSON* j_object   = cJSON_GetObjectItemCaseSensitive(j_params, "object");
    const cJSON* j_data     = cJSON_GetObjectItemCaseSensitive(j_params, "data");
    const cJSON* j_name     = cJSON_GetObjectItemCaseSensitive(j_params, "name");
    const cJSON* j_seq      = cJSON_GetObjectItemCaseSensitive(j_params, "seq");
    const cJSON* j_duration = cJSON_GetObjectItemCaseSensitive(j_params, "duration");
    const cJSON* j_enable   = cJSON_GetObjectItemCaseSensitive(j_params, "enable");

    const cJSON_bool e_type     = cJSON_IsNumber(j_type);
    const cJSON_bool e_group    = cJSON_IsString(j_group);
    const cJSON_bool e_qq       = cJSON_IsString(j_qq);
    const cJSON_bool e_content  = cJSON_IsString(j_content);
    const cJSON_bool e_msgid    = cJSON_IsString(j_msgid);
    const cJSON_bool e_message  = cJSON_IsString(j_message);
    const cJSON_bool e_object   = cJSON_IsString(j_object);
    const cJSON_bool e_data     = cJSON_IsString(j_data);
    const cJSON_bool e_name     = cJSON_IsString(j_name);
    const cJSON_bool e_seq      = cJSON_IsString(j_seq);
    const cJSON_bool e_duration = cJSON_IsNumber(j_duration);
    const cJSON_bool e_enable   = cJSON_IsBool(j_enable);

    int         v_type     = e_type     ?  j_type->valueint        :  -1;
    const char* v_group    = e_group    ?  j_group->valuestring    :  NULL;
    const char* v_qq       = e_qq       ?  j_qq->valuestring       :  NULL;
    const char* v_content  = e_content  ?  j_content->valuestring  :  NULL;
    const char* v_msgid    = e_msgid    ?  j_msgid->valuestring    :  NULL;
    const char* v_message  = e_message  ?  j_message->valuestring  :  NULL;
    const char* v_object   = e_object   ?  j_object->valuestring   :  NULL;
    const char* v_data     = e_data     ?  j_data->valuestring     :  NULL;
    const char* v_name     = e_name     ?  j_name->valuestring     :  NULL;
    const char* v_seq      = e_seq      ?  j_seq->valuestring      :  NULL;
    int         v_duration = e_duration ?  j_duration->valueint    :  -1;
    bool        v_enable   = e_enable   ?  cJSON_IsTrue(j_enable)  : false;
 
    pluginLog("jsonRPC", "Client call '%s' method", v_method);

    #define PARAMS_CHECK(condition) if(!(condition)) {pluginLog("jsonParse", "Invalid data"); goto RPCParseEnd;}
    #define METHOD_IS(name) (strcmp(name, v_method) == 0)
    
    if(METHOD_IS("sendMessage")) {

        PARAMS_CHECK(e_type && e_content && (e_qq || e_group));

        char* gbkText = UTF8ToGBK(v_content);
        QL_sendMessage(v_type, e_content ? v_group : "", e_qq ? v_qq : "", gbkText, authCode);
        free((void*)gbkText);

    } else if (METHOD_IS("withdrawMessage")) {

        PARAMS_CHECK(e_group && e_msgid);

        QL_withdrawMessage(v_group, v_msgid, authCode);

    } else if (METHOD_IS("getFriendList")) {

        PARAMS_CHECK(e_id);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* friendList = GBKToUTF8(QL_getFriendList(authCode));
        cJSON_AddItemToObject(root, "result", cJSON_Parse(friendList));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)friendList);
        free((void*)jsonStr);

    } else if (METHOD_IS("addFriend")) {

        PARAMS_CHECK(e_qq);

        if(!e_message) {
            QL_addFriend(v_qq, "", authCode);
        } else {
            const char* text = UTF8ToGBK(v_message);
            QL_addFriend(v_qq, text, authCode);
            free((void*)text);
        }

    } else if (METHOD_IS("deleteFriend")) {

        PARAMS_CHECK(e_qq);

        QL_deleteFriend(v_qq, authCode);

    } else if (METHOD_IS("getGroupList")) {

        PARAMS_CHECK(e_id);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* groupList = GBKToUTF8(QL_getGroupList(authCode));
        cJSON_AddItemToObject(root, "result", cJSON_Parse(groupList));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)groupList);
        free((void*)jsonStr);

    } else if (METHOD_IS("getGroupMemberList")) {

        PARAMS_CHECK(e_id && e_group);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* groupMemberList = GBKToUTF8(QL_getGroupMemberList(v_group, authCode));
        cJSON_AddItemToObject(root, "result", cJSON_Parse(groupMemberList));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)groupMemberList);
        free((void*)jsonStr);

    } else if (METHOD_IS("addGroup")) {

        PARAMS_CHECK(e_group);

        if(!e_message) {
            QL_addGroup(v_group, "", authCode);
        } else {
            const char* text = UTF8ToGBK(v_message);
            QL_addGroup(v_group, text, authCode);
            free((void*)text);
        }

    } else if (METHOD_IS("quitGroup")) {

        PARAMS_CHECK(e_group);

        QL_quitGroup(v_group, authCode);

    } else if (METHOD_IS("getGroupCard")) {

        PARAMS_CHECK(e_id && e_group && e_qq);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* groupCard = GBKToUTF8(QL_getGroupCard(v_group, v_qq, authCode));
        cJSON_AddItemToObject(root, "result", cJSON_CreateString(groupCard));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)groupCard);
        free((void*)jsonStr);

    } else if (METHOD_IS("uploadImage")) {

        PARAMS_CHECK(e_id && e_type && e_object && e_data);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* text = QL_uploadImage(v_type, v_object, v_data, authCode);
        int textLen = strlen(text);

        if(textLen > 9 && textLen < 100 && strstr(text, "[QQ:pic=") == text) {
            char guid[textLen + 1];
            strcpy(guid, text);
            guid[textLen - 1] = '\0';   // 去除末尾的']'
            cJSON_AddItemToObject(root, "result", cJSON_CreateString(guid + 8));    // 去除开头的'[QQ:pic='
        } else {
            cJSON_AddItemToObject(root, "result", cJSON_CreateString(""));
        }

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)jsonStr);

    } else if (METHOD_IS("getQQInfo")) {

        PARAMS_CHECK(e_id && e_qq);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* info = GBKToUTF8(QL_getQQInfo(v_qq, authCode));
        cJSON_AddItemToObject(root, "result", cJSON_Parse(info));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)info);
        free((void*)jsonStr);

    } else if (METHOD_IS("getGroupInfo")) {

        PARAMS_CHECK(e_id && e_group);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* info = GBKToUTF8(QL_getGroupInfo(v_group, authCode));
        cJSON_AddItemToObject(root, "result", cJSON_Parse(info));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)info);
        free((void*)jsonStr);

    } else if (METHOD_IS("inviteIntoGroup")) {

        PARAMS_CHECK(e_qq && e_group);

        QL_inviteIntoGroup(v_group, v_qq, authCode);

    } else if (METHOD_IS("setGroupCard")) {

        PARAMS_CHECK(e_qq && e_group && e_name);

        QL_setGroupCard(v_group, v_qq, v_name, authCode);

    } else if (METHOD_IS("getLoginAccount")) {

        PARAMS_CHECK(e_id);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* account = GBKToUTF8(QL_getLoginAccount(authCode));
        cJSON_AddItemToObject(root, "result", cJSON_CreateString(account));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)account);
        free((void*)jsonStr);

    } else if (METHOD_IS("setSignature")) {

        PARAMS_CHECK(e_content);

        QL_setSignature(v_content, authCode);

    } else if (METHOD_IS("getNickname")) {

        PARAMS_CHECK(e_id && e_qq);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* nickname = GBKToUTF8(QL_getNickname(v_qq, authCode));
        cJSON_AddItemToObject(root, "result", cJSON_CreateString(nickname));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)nickname);
        free((void*)jsonStr);

    } else if (METHOD_IS("getPraiseCount")) {

        PARAMS_CHECK(e_id && e_qq);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* count = GBKToUTF8(QL_getPraiseCount(v_qq, authCode));
        cJSON_AddItemToObject(root, "result", cJSON_CreateString(count));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)count);
        free((void*)jsonStr);

    } else if (METHOD_IS("givePraise")) {

        PARAMS_CHECK(e_qq);

        QL_givePraise(v_qq, authCode);

    } else if (METHOD_IS("handleFriendRequest")) {

        PARAMS_CHECK(e_qq && e_type);

        if(e_message) {
            const char* message = UTF8ToGBK(v_message);
            QL_handleFriendRequest(v_qq, v_type, message, authCode);
            free((void*)message);
        } else {
            QL_handleFriendRequest(v_qq, v_type, "", authCode);
        }

    } else if (METHOD_IS("setState")) {

        PARAMS_CHECK(e_type);

        QL_setState(v_type, authCode);

    } else if (METHOD_IS("handleGroupRequest")) {

        PARAMS_CHECK(e_group && e_qq && e_seq && e_type);

        const char* message = e_message ? v_message : "";

        QL_handleGroupRequest(v_group, v_qq, v_seq, v_type, message, authCode);

    } else if (METHOD_IS("kickGroupMember")) {

        PARAMS_CHECK(e_group && e_qq);

        QL_kickGroupMember(v_group, v_qq, false, authCode);

    } else if (METHOD_IS("silence")) {

        PARAMS_CHECK(e_group && e_qq && e_duration);

        QL_silence(v_group, v_qq, v_duration, authCode);

    } else if (METHOD_IS("globalSilence")) {

        PARAMS_CHECK(e_group && e_enable);

        QL_globalSilence(v_group, v_enable, authCode);

    } else {
        pluginLog("jsonRPC", "Unknown method '%s'", v_method);
    }

    RPCParseEnd:

    cJSON_Delete(json);
}

// 不存在配置文件时创建配置文件并写入全局变量config的默认配置
void createConfigFile(void) {

    char path[sizeof(pluginPath) + 20];
    strcpy(path, pluginPath);
    strcat(path, "config.json");

    // 不存在配置文件
    if(access(path, F_OK) == -1) {
        
        FILE* fp = fopen(path, "wb");
        
        if(fp == NULL) {
            pluginLog("createConfigFile", "Failed to open file");
            return;
        }
        
        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "port", cJSON_CreateNumber(config.port));
        cJSON_AddItemToObject(root, "path", cJSON_CreateString(config.path));

        const char* json = cJSON_Print(root);
        fwrite(json, strlen(json), 1, fp);

        fclose(fp);
        cJSON_Delete(root);
        free((void*)json);
    }
}

// 读取配置文件并覆盖全局变量config的默认配置
void readConfigFile(void) {
    
    char path[sizeof(pluginPath) + 20];
    strcpy(path, pluginPath);
    strcat(path, "config.json");

    FILE* fp = fopen(path, "rb");

    if(fp == NULL) {
        pluginLog("readConfigFile", "Failed to open file");
        return;
    }

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);
    
    char buff[size + 1];
    if(fread(buff, size, 1, fp) != 1) {
        pluginLog("readConfigFile", "Failed to read file");
        fclose(fp);
        return;
    }
    buff[size] = '\0';
    
    cJSON* json = cJSON_Parse(buff);
    if(json == NULL) {
        pluginLog("readConfigFile", "Failed to parse json");
        cJSON_Delete(json);
        fclose(fp);
        return;
    }

    cJSON* j_port = cJSON_GetObjectItem(json, "port");
    cJSON* j_path = cJSON_GetObjectItem(json, "path");
    
    if(cJSON_IsNumber(j_port)) {
        config.port = (u_short)j_port->valueint;
    }

    if(cJSON_IsString(j_path)) {
        strncpy(config.path, j_path->valuestring, sizeof(config.path) - 1);
        config.path[sizeof(config.path) - 1] = '\0';
    }

    cJSON_Delete(json);
    fclose(fp);
}

DllExport(const char*) Information(const char* _authCode) {
    
    // 获取authCode
    strncpy(authCode, _authCode, sizeof(authCode) - 1);
    
    return PLUGIN_INFO;
}

DllExport(int) Event_Initialization(void) {
    
    // 获取插件目录
    const char* path = QL_getPluginPath(authCode);

    if(strlen(path) > sizeof(pluginPath) - 1) {
        pluginPath[0] = '\0';
        pluginLog("Event_Initialization", "The plugin directory path length is too long");
    } else {
        strcpy(pluginPath, path);
    }

    pluginLog("Event_Initialization", "Plugin directory is %s", pluginPath);

    return 0;
}

DllExport(int) Event_pluginStart(void) {
    
    createConfigFile();
    readConfigFile();

    int result = serverStart(config.port, config.path);
    
    if(result != 0) {
        pluginLog("Event_pluginStart", "WebSocket server startup failed");
        const char* msg = UTF8ToGBK("WebSocket服务器启动失败，请尝试修改服务器监听端口并刷新插件重试");
        MessageBoxA(NULL, msg, "WebSocket Plugin", MB_OK | MB_ICONERROR);
        free((void*)msg);
    } else {
        pluginLog("Event_pluginStart", "WebSocket server startup success");
    }
    
    return 0;
} 

DllExport(int) Event_pluginStop(void) {
    
    serverStop();
    
    pluginLog("Event_pluginStop", "WebSocket server stopped"); 
    
    return 0;
}

DllExport(int) Event_GetNewMsg (
    int type,              // 1=好友消息 2=群消息 3=群临时消息 4=讨论组消息 5=讨论组临时消息 6=QQ临时消息
    const char* group,     // 类型为1或6的时候，此参数为空字符串，其余情况下为群号或讨论组号
    const char* qq,        // 消息来源QQ号 "10000"都是来自系统的消息(比如某人被禁言或某人撤回消息等)
    const char* msg,       // 消息内容
    const char* msgid      // 消息id，撤回消息的时候会用到，群消息会存在，其余情况下为空  
) {

    // 将可能为NULL的字符串指针参数修改为空字符串
    group = group ? group : "";
    msgid = msgid ? msgid : "";

    const char* u8Content = GBKToUTF8(msg);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "event", cJSON_CreateString("message"));

    cJSON* params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "type", cJSON_CreateNumber(type));
    cJSON_AddItemToObject(params, "msgid", cJSON_CreateString(msgid));
    cJSON_AddItemToObject(params, "group", cJSON_CreateString(group));
    cJSON_AddItemToObject(params, "qq", cJSON_CreateString(qq));
    cJSON_AddItemToObject(params, "content", cJSON_CreateString(u8Content));

    cJSON_AddItemToObject(root, "params", params);

    const char* jsonStr = cJSON_PrintUnformatted(root);

    wsFrameSendToAll(jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)u8Content);
    free((void*)jsonStr);

    return 0;    // 返回0下个插件继续处理该事件，返回1拦截此事件不让其他插件执行
}

DllExport(int) Event_AddFrinend(const char* qq, const char* message) {

    const char* u8Message = GBKToUTF8(message);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "event", cJSON_CreateString("friendRequest"));

    cJSON* params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "qq", cJSON_CreateString(qq));
    cJSON_AddItemToObject(params, "message", cJSON_CreateString(u8Message));

    cJSON_AddItemToObject(root, "params", params);

    const char* jsonStr = cJSON_PrintUnformatted(root);

    wsFrameSendToAll(jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)u8Message);
    free((void*)jsonStr);

    return 0;
}

DllExport(int) Event_BecomeFriends(const char* qq) {

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "event", cJSON_CreateString("becomeFriends"));

    cJSON* params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "qq", cJSON_CreateString(qq));

    cJSON_AddItemToObject(root, "params", params);

    const char* jsonStr = cJSON_PrintUnformatted(root);

    wsFrameSendToAll(jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)jsonStr);

    return 0;
}

void handleGroupMemberChange(
    int type, 
    const char* group, 
    const char* qq, 
    const char* operator,
    const char* event
) {

    // 将可能为NULL的字符串指针参数修改为空字符串
    operator = operator ? operator : "";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "event", cJSON_CreateString(event));

    cJSON* params = cJSON_CreateObject();
    
    cJSON_AddItemToObject(params, "type", cJSON_CreateNumber(type));
    cJSON_AddItemToObject(params, "group", cJSON_CreateString(group));
    cJSON_AddItemToObject(params, "qq", cJSON_CreateString(qq));
    cJSON_AddItemToObject(params, "operator", cJSON_CreateString(operator));
    
    cJSON_AddItemToObject(root, "params", params);

    const char* jsonStr = cJSON_PrintUnformatted(root);

    wsFrameSendToAll(jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)jsonStr);
}

DllExport(int) Event_GroupMemberIncrease(
    int type,               // 1=主动加群、2=被管理员邀请
    const char* group,      // 
    const char* qq,         // 
    const char* operator    // 操作者QQ
) {
    handleGroupMemberChange(type, group, qq, operator, "groupMemberIncrease");
    return 0;
}

DllExport(int) Event_GroupMemberDecrease(
    int type,               // 1=主动退群、2=被管理员踢出
    const char* group,      // 
    const char* qq,         // 
    const char* operator    // 操作者QQ，仅在被管理员踢出时存在
) {
    handleGroupMemberChange(type, group, qq, operator, "groupMemberDecrease");
    return 0;
}

DllExport(int) Event_AdminChange(
    int type,               // 1=成为管理 2=被解除管理
    const char* group,
    const char* qq
) {

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "event", cJSON_CreateString("adminChange"));

    cJSON* params = cJSON_CreateObject();
    
    cJSON_AddItemToObject(params, "type", cJSON_CreateNumber(type));
    cJSON_AddItemToObject(params, "group", cJSON_CreateString(group));
    cJSON_AddItemToObject(params, "qq", cJSON_CreateString(qq));
    
    cJSON_AddItemToObject(root, "params", params);

    const char* jsonStr = cJSON_PrintUnformatted(root);

    wsFrameSendToAll(jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)jsonStr);

    return 0;
}

DllExport(int) Event_AddGroup(
    int type,               // 1=主动加群、2=某人被邀请进群、3=机器人被邀请进群
    const char* group,      //
    const char* qq,         //
    const char* operator,   // 邀请者QQ，主动加群时不存在
    const char* message,    // 加群附加消息，只有主动加群时存在
    const char* seq         // seq，同意加群时需要用到
) {

    // 将可能为NULL的字符串指针参数修改为空字符串
    operator = operator ? operator : "";
    message  = message  ? message  : "";
    seq      = seq      ? seq      : "";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "event", cJSON_CreateString("groupRequest"));

    cJSON* params = cJSON_CreateObject();

    const char* u8Message = GBKToUTF8(message ? message : "");
    
    cJSON_AddItemToObject(params, "type", cJSON_CreateNumber(type));
    cJSON_AddItemToObject(params, "group", cJSON_CreateString(group));
    cJSON_AddItemToObject(params, "qq", cJSON_CreateString(qq));
    cJSON_AddItemToObject(params, "operator", cJSON_CreateString(operator));
    cJSON_AddItemToObject(params, "message", cJSON_CreateString(u8Message));
    cJSON_AddItemToObject(params, "seq", cJSON_CreateString(seq));
    
    cJSON_AddItemToObject(root, "params", params);

    const char* jsonStr = cJSON_PrintUnformatted(root);

    wsFrameSendToAll(jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)u8Message);
    free((void*)jsonStr);

    return 0;
}

DllExport(int) Event_GetQQWalletData(
    int type,               // 1=好友转账、2=群临时会话转账、3=讨论组临时会话转账
    const char* group,      // type为1时此参数为空，type为2、3时分别为群号或讨论组号
    const char* qq,         // 转账者QQ
    const char* amount,     // 转账金额
    const char* message,    // 转账备注消息
    const char* id          // 转账订单号
) {

    // 将可能为NULL的字符串指针参数修改为空字符串
    group     = group    ? group    : "";
    message   = message  ? message  : "";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "event", cJSON_CreateString("receiveMoney"));
    
    cJSON* params = cJSON_CreateObject();
    const char* u8Message = GBKToUTF8(message ? message : "");

    cJSON_AddItemToObject(params, "type", cJSON_CreateNumber(type));
    cJSON_AddItemToObject(params, "group", cJSON_CreateString(group));
    cJSON_AddItemToObject(params, "qq", cJSON_CreateString(qq));
    cJSON_AddItemToObject(params, "amount", cJSON_CreateString(amount));
    cJSON_AddItemToObject(params, "message", cJSON_CreateString(u8Message));
    cJSON_AddItemToObject(params, "id", cJSON_CreateString(id));
    
    cJSON_AddItemToObject(root, "params", params);

    const char* jsonStr = cJSON_PrintUnformatted(root);

    wsFrameSendToAll(jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)u8Message);
    free((void*)jsonStr);

    return 0;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    
    if(loadQQLightAPI() != 0) {
        MessageBox(NULL, "The message.dll load failed", "error", MB_OK);
        return FALSE;
    }
    
    /* Returns TRUE on success, FALSE on failure */
    return TRUE;
}
