#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <winsock2.h>
#include <time.h>
#include "lib/cjson/cJSON.h"
#include "api.h"
#include "ws.h"

#define DllExport(returnType) __declspec(dllexport) returnType __stdcall

const char* PLUGIN_INFO = 
    "pluginID=websocket.protocol;\r\n"
    "pluginName=WebSocket Protocol;\r\n"
    "pluginBrief="
        "Enable you to use QQLight API in any language you like via WebSocket.\r\n\r\n"
        "GitHub:\r\nhttps://github.com/Chocolatl/qqlight-websocket;\r\n"
    "pluginVersion=0.1.1;\r\n"
    "pluginSDK=3;\r\n"
    "pluginAuthor=Chocolatl;\r\n"
    "pluginWindowsTitle=;"
;

char authCode[64];
char pluginPath[1024];

typedef enum Protocol {
    socketProtocol,
    websocketProtocol
} Protocol;

typedef struct Client {
    Protocol protocol;
    SOCKET socket;
    WsFrame wsFrame;    // 仅在升级协议后使用
} Client;

#define MAX_CLIENT_NUM FD_SETSIZE
typedef struct ClientSockets {
    int    total;
    Client clients[MAX_CLIENT_NUM];
} ClientSockets;


void pluginLog(const char* type, const char* format, ...) {
    
    // 获取当前时间
    time_t timer;
    char datetime[26];
    struct tm* tm_info;

    time(&timer);
    tm_info = localtime(&timer);
    strftime(datetime, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    // 获取日志内容
    char content[512];
    va_list arg;

    va_start(arg, format);
    vsnprintf(content, sizeof(content) - 1, format, arg);
    va_end(arg);

    // 拼接
    char line[640];
    int len = sprintf(line, "%s | %-20s | %s\r\n", datetime, type, content);

    FILE *fp;
    char path[1048];

    sprintf(path, "%s%s", pluginPath, "log.txt");

    if((fp = fopen(path, "a+")) == NULL) {
        return;
    }

    fwrite(line, len, 1, fp);

    fclose(fp);
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
    wchar_t* u16str[n + 1];
    MultiByteToWideChar(CP_UTF8, 0, str, -1, u16str, n);

    n = WideCharToMultiByte(CODE_PAGE, 0, u16str, -1, NULL, 0, NULL, NULL);
    char* gbstr = malloc(n + 1);
    WideCharToMultiByte(CODE_PAGE,0, u16str, -1, gbstr, n, NULL, NULL);

    return gbstr;
}

// 将数据转换成WebSocket帧并发送
int wsFrameSend(SOCKET socket, const char* buff, int len, FrameType type) {

    int newLen;
    const char* frame = convertToWebSocketFrame(buff, type, len, &newLen);

    int iSendResult = send(socket, frame, newLen, 0);

    if(iSendResult == SOCKET_ERROR) {
        pluginLog("wsFrameSend", "Send failed: %d", WSAGetLastError());
        return iSendResult;
    }

    free((void*)frame);
    
    pluginLog("wsFrameSend", "Bytes sent: %d", iSendResult);
    return iSendResult;
}

void wsFrameSendToAll(ClientSockets* clientSockets, const char* buff, int len,  FrameType type) {
    for(int i = 0; i < clientSockets->total; i++) {
        pluginLog("wsFrameSendToAll", "Send data to %dst client", i);
        wsFrameSend(clientSockets->clients[i].socket, buff, len, type);
    }
}

void wsClientTextDataHandle(const char* payload, uint64_t payloadLen, Client* client) {
    
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
    const cJSON* j_type    = cJSON_GetObjectItemCaseSensitive(j_params, "type");        // 即使j_params为NULL也是安全的，返回的结果也是NULL
    const cJSON* j_group   = cJSON_GetObjectItemCaseSensitive(j_params, "group");
    const cJSON* j_qq      = cJSON_GetObjectItemCaseSensitive(j_params, "qq");
    const cJSON* j_content = cJSON_GetObjectItemCaseSensitive(j_params, "content");
    const cJSON* j_msgid   = cJSON_GetObjectItemCaseSensitive(j_params, "msgid");
    const cJSON* j_message = cJSON_GetObjectItemCaseSensitive(j_params, "message");
    const cJSON* j_object  = cJSON_GetObjectItemCaseSensitive(j_params, "object");
    const cJSON* j_data    = cJSON_GetObjectItemCaseSensitive(j_params, "data");

    const cJSON_bool e_type    = cJSON_IsNumber(j_type);
    const cJSON_bool e_group   = cJSON_IsString(j_group);
    const cJSON_bool e_qq      = cJSON_IsString(j_qq);
    const cJSON_bool e_content = cJSON_IsString(j_content);
    const cJSON_bool e_msgid   = cJSON_IsString(j_msgid);
    const cJSON_bool e_message = cJSON_IsString(j_message);
    const cJSON_bool e_object  = cJSON_IsString(j_object);
    const cJSON_bool e_data    = cJSON_IsString(j_data);

    int         v_type    = e_type    ?  j_type->valueint        :  -1;
    const char* v_group   = e_group   ?  j_group->valuestring    :  NULL;
    const char* v_qq      = e_qq      ?  j_qq->valuestring       :  NULL;
    const char* v_content = e_content ?  j_content->valuestring  :  NULL;
    const char* v_msgid   = e_msgid   ?  j_msgid->valuestring    :  NULL;
    const char* v_message = e_message ?  j_message->valuestring  :  NULL;
    const char* v_object  = e_object  ?  j_object->valuestring   :  NULL;
    const char* v_data    = e_data    ?  j_data->valuestring     :  NULL;
 
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
        wsFrameSend(client->socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)friendList);
        free((void*)jsonStr);

    } else if (METHOD_IS("addFriend")) {

        PARAMS_CHECK(e_id);

        if(!e_message) {
            QL_addFriend(v_qq, "", authCode);
        } else {
            const char* text = UTF8ToGBK(v_message);
            QL_addFriend(v_qq, text, authCode);
            free((void*)text);
        }

    } else if (METHOD_IS("deleteFriend")) {

        PARAMS_CHECK(e_id);

        QL_deleteFriend(v_qq, authCode);

    } else if (METHOD_IS("getGroupList")) {

        PARAMS_CHECK(e_id);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* groupList = GBKToUTF8(QL_getGroupList(authCode));
        cJSON_AddItemToObject(root, "result", cJSON_Parse(groupList));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(client->socket, jsonStr, strlen(jsonStr), frameType_text);

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
        wsFrameSend(client->socket, jsonStr, strlen(jsonStr), frameType_text);

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
        wsFrameSend(client->socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)groupCard);
        free((void*)jsonStr);

    } else if (METHOD_IS("uploadImage")) {

        PARAMS_CHECK(e_id && e_type && e_object && e_data);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "id", cJSON_CreateString(v_id));

        const char* guid = QL_uploadImage(v_type, v_object, v_data, authCode);
        cJSON_AddItemToObject(root, "result", cJSON_CreateString(guid));

        const char* jsonStr = cJSON_PrintUnformatted(root);
        wsFrameSend(client->socket, jsonStr, strlen(jsonStr), frameType_text);

        cJSON_Delete(root);
        free((void*)jsonStr);

    } else {
        pluginLog("jsonRPC", "Unknown method '%s'", v_method);
    }

    RPCParseEnd:

    cJSON_Delete(json);
}

// 处理WebSocket帧数据，返回-1代表需要关闭连接
int wsClientDataHandle(const char* recvBuff, int recvLen, Client* client) {

    WsFrame* wsFrame = &client->wsFrame;

    if(recvLen == 0) {
        return 0;
    }

    int consume = readWebSocketFrameStream(wsFrame, recvBuff, recvLen);

    pluginLog("wsClientDataHandle", "Consume %d bytes of data in %d bytes", consume, recvLen);
    pluginLog("wsClientDataHandle", "wsFrame->state is %d", wsFrame->state);

    if(wsFrame->state == frameState_success) {

        pluginLog("wsClientDataHandle", "Header and payload lengths are %llu and %llu", wsFrame->headerLen, wsFrame->payloadLen);

        // 暂时不处理多帧数据，遇到多帧数据关闭连接
        if(wsFrame->FIN == 0) {
            pluginLog("wsClientDataHandle", "This is not the final fragment in a message");
            return -1;
        }

        // 客户端希望关闭连接
        if(wsFrame->frameType == frameType_connectionClose) {
            pluginLog("wsClientDataHandle", "Connection close frame");
            return -1;
        }

        // 遇到意料之外的帧类型
        if(wsFrame->frameType == frameType_binary      ||
           wsFrame->frameType == frameType_pong        ||
           wsFrame->frameType == frameType_continuation
        ) {
            pluginLog("wsClientDataHandle", "Unexpected frame type");
            return -1;
        }

        uint64_t payloadLen = wsFrame->payloadLen;
        u_char* payload = wsFrame->buff + wsFrame->headerLen;

        // 解码载荷
        for(uint64_t j = 0; j < payloadLen; j++) {
            payload[j] = payload[j] ^ wsFrame->mask[j % 4];
        }

        int iSendResult;

        // 心跳
        if(wsFrame->frameType == frameType_ping) {
            pluginLog("wsClientDataHandle", "pong");
            wsFrameSend(client->socket, payload, payloadLen, frameType_pong);
        }

        // 处理文本数据
        if(wsFrame->frameType == frameType_text) {
            wsClientTextDataHandle(payload, payloadLen, client);
        }

    }

    // 一个帧接收完成并处理完毕后释放内存
    if(wsFrame->state == frameState_success) {
        freeWebSocketFrame(wsFrame);
    }

    // 解析ws帧出错，释放内存并通知关闭连接
    if (wsFrame->state == frameState_failure) {
        freeWebSocketFrame(wsFrame);
        return -1;
    }

    // 传入的数据不止包含当前帧，包含下一帧的数据
    if(consume != recvLen) {
        return wsClientDataHandle(recvBuff + consume, recvLen - consume, client);
    }

    return 0;
}

void receiveComingData(ClientSockets* clientSockets) {

    #define RECV_BUFLEN 40960

    unsigned char recvbuf[RECV_BUFLEN];
    int iResult;

    int ret; 
    fd_set fdread; 
    struct timeval tv = {1, 0}; 
 
    receivingDataLoop:
        
    FD_ZERO(&fdread); 
    for(int i = 0; i < clientSockets->total; i++) {     
        FD_SET(clientSockets->clients[i].socket, &fdread);   
    }
    
    ret = select(0, &fdread, NULL, NULL, &tv);
    
    if(ret == 0) {
        goto receivingDataLoop;     // select的等待时间到达，开始下一轮等待 
    }
    
    // 当客户端数为0时select的等待时间设置将不会生效，立即返回WSAEINVAL错误
    // 所以客户端数为0时会产生无停顿的循环，占满CPU，这里通过Sleep放弃时间片占用 
    if(ret == SOCKET_ERROR && WSAGetLastError() == WSAEINVAL) {
        Sleep(1); 
    }
    
    for(int i = 0; i < clientSockets->total; i++) {
        
        const SOCKET clientSocket = clientSockets->clients[i].socket;
        
        if(FD_ISSET(clientSocket, &fdread)) {
            
            iResult = recv(clientSocket, recvbuf, RECV_BUFLEN, 0);  

            if(iResult > 0) {
                
                pluginLog("receiveComingData", "Bytes received: %d", iResult);
                
                if(clientSockets->clients[i].protocol == websocketProtocol) {
                    int result = wsClientDataHandle(recvbuf, iResult, &clientSockets->clients[i]);
                    if(result == -1) goto removeClientSocket;
                } else {
                    int result = wsShakeHands(recvbuf, iResult, clientSockets->clients[i].socket);
                    if(result != 0) {
                        goto removeClientSocket;
                    } else {
                        clientSockets->clients[i].protocol = websocketProtocol;
                        initWsFrameStruct(&clientSockets->clients[i].wsFrame);        // 初始化ws帧结构
                    }
                }

            } else {
                
                if(iResult == 0) {
                    // 客户端礼貌的关闭连接 
                    pluginLog("receiveComingData", "Connection closing...");
                } else {
                    // 客户端异常关闭连接等情况
                    pluginLog("receiveComingData", "Recv failed: %d", WSAGetLastError());
                }
                
                // 从数组中移除该socket并调用closesocket 
                removeClientSocket:
                
                // 该socket不处于数组末尾 
                if(i < clientSockets->total - 1) {
                    
                    clientSockets->clients[i] = 
                        clientSockets->clients[--clientSockets->total];  // 将数组末尾的socket填到当前位置 
                                                
                    i--;        // 回退循环计数，从当前位置继续循环 

                } else {
                    clientSockets->total--;
                    i--;
                }
                
                pluginLog("receiveComingData", "Now client sockets length: %d", clientSockets->total);
                
                struct linger so_linger;
                so_linger.l_onoff = 1;
                so_linger.l_linger = 1;
                setsockopt(clientSocket, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
                closesocket(clientSocket);
            }
        }
    }

    goto receivingDataLoop;
}

SOCKET serverSocket;

void receiveConnect(ClientSockets* clientSockets) {
    
    DWORD   dwThreadId;
    SOCKET     clientSocket;
    
    HANDLE hHandle = CreateThread(NULL, 0, (void*)receiveComingData, clientSockets, 0, &dwThreadId);
    
    SOCKADDR_IN client; 
    
    receivingConnectLoop:

    // Accept a connection   
    clientSocket = accept(serverSocket, (struct sockaddr *)&client, NULL);   

    // 当连接数达到上限时拒绝连接
    if(clientSockets->total >= MAX_CLIENT_NUM) {
        closesocket(clientSocket);
        goto receivingConnectLoop;
    }
    
    if(clientSocket == INVALID_SOCKET) {
            
        int errCode = WSAGetLastError();
        
        // serverSocket不是一个套接字，即serverSocket已经执行了closesocket 
        if(errCode == WSAENOTSOCK) {
            pluginLog("receiveConnect", "Threads will exit");
            TerminateThread(hHandle, NULL);        // 粗暴的终止receiveComingData线程 
            
            pluginLog("receiveConnect", "Closing all client sockets...");

            // 关闭所有客户端连接 
            for(int i = 0; i < clientSockets->total; i++) {
                closesocket(clientSockets->clients[i].socket);       
            }
            clientSockets->total = 0;
            
            ExitThread(NULL);     // 退出 
        }
        
        pluginLog("receiveConnect", "Accept failed: %d", WSAGetLastError());
        goto receivingConnectLoop;
    }
    
    pluginLog("receiveConnect", "Accepted client: %s:%d", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
    
    // Add socket to fdTotal
    clientSockets->clients[clientSockets->total].socket = clientSocket;
    clientSockets->clients[clientSockets->total].protocol = socketProtocol;
    clientSockets->total++;

    goto receivingConnectLoop;
}

ClientSockets clientSockets = {total: 0};

int serverStart(void) {

    #define SERVER_PORT 49632
    WSADATA wsaData;
    
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if(iResult != 0) {
        pluginLog("ServerStart", "WSAStartup failed");
        return -1;
    }
    
    struct sockaddr_in sockAddr;
    
    ZeroMemory(&sockAddr, sizeof(sockAddr));
    sockAddr.sin_family = PF_INET;
    sockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    sockAddr.sin_port = htons(SERVER_PORT);
    
    serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(serverSocket == INVALID_SOCKET) {
        pluginLog("ServerStart", "Error at socket(): %d", WSAGetLastError());
        WSACleanup();
        return -1;
    }
    
    if(bind(serverSocket, (SOCKADDR*)&sockAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        pluginLog("ServerStart","Bind failed with error: %d", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return -1;
    }
    
    if(listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        pluginLog("ServerStart", "Listen failed with error: %d", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return -1;
    }
    
    DWORD dwThreadId;
    HANDLE hHandle = CreateThread(NULL, 0, (void*)receiveConnect, &clientSockets, 0, &dwThreadId);
    
    return 0;
}

void serverStop(void) {
    closesocket(serverSocket);
    WSACleanup();
}

DllExport(const char*) __stdcall Information(const char* _authCode) {
    
    // 获取authCode
    strncpy(authCode, _authCode, sizeof(authCode) - 1);
    
    return PLUGIN_INFO;
}

DllExport(int) Event_Initialization(void) {
    
    // 获取插件目录
    strncpy(pluginPath, QL_getPluginPath(authCode), sizeof(authCode) - 1);
    
    return 0;
}

DllExport(int) Event_pluginStart(void) {
    
    int result = serverStart();
    
    if(result != 0) {
        pluginLog("Event_pluginStart", "WebSocket server startup failed");
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

    wsFrameSendToAll(&clientSockets, jsonStr, strlen(jsonStr), frameType_text);

    cJSON_Delete(root);
    free((void*)u8Content);
    free((void*)jsonStr);

    return 0;    // 返回0下个插件继续处理该事件，返回1拦截此事件不让其他插件执行
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    
    if(loadQQLightAPI() != 0) {
        MessageBox(NULL, "The message.dll load failed", "error", MB_OK);
        return FALSE;
    }
    
    /* Returns TRUE on success, FALSE on failure */
    return TRUE;
}
