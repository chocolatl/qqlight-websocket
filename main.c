#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <winsock2.h>
#include <time.h>
#include "lib/cjson/cJSON.h"
#include "lib/sha1/sha1.h"
#include "lib/base64/b64.h"
#include "api.h"
#include "wsFrame.h"

#define DllExport(returnType) __declspec(dllexport) returnType __stdcall

const char* PLUGIN_INFO = 
	"pluginID=websocket.protocol;\r\n"
	"pluginName=WebSocket Protocol;\r\n"
	"pluginBrief="
		"Enable you to use QQLight API in any language you like via WebSocket.\r\n\r\n"
		"GitHub:\r\nhttps://github.com/Chocolatl/qqlight-websocket;\r\n"
	"pluginVersion=0.1.0;\r\n"
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
	WsFrame wsFrame;	// 仅在升级协议后使用
} Client;

#define MAX_CLIENT_NUM FD_SETSIZE
typedef struct ClientSockets {
	int    total;
    Client clients[MAX_CLIENT_NUM];
} ClientSockets;


void log(const char* type, const char* format, ...) {
	
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
	#define CODE_PAGE 54936

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
	#define CODE_PAGE 54936

    int n = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    wchar_t* u16str[n + 1];
    MultiByteToWideChar(CP_UTF8, 0, str, -1, u16str, n);

    n = WideCharToMultiByte(CODE_PAGE, 0, u16str, -1, NULL, 0, NULL, NULL);
    char* gbstr = malloc(n + 1);
    WideCharToMultiByte(CODE_PAGE,0, u16str, -1, gbstr, n, NULL, NULL);

	return gbstr;
}

int socketSend(SOCKET socket, const char* buff, int len) {

	int iSendResult = send(socket, buff, len, 0);

	if(iSendResult == SOCKET_ERROR) {
		log("socketSend", "Send failed: %d", WSAGetLastError());
		return iSendResult;
	}
	
	log("socketSend", "Bytes sent: %d", iSendResult);
	return iSendResult;
}

int getSecWebSocketAcceptKey(const char* key, char* b64buff, int len) {

    SHA1_CTX ctx;
    unsigned char hash[20], buff[512];

    if(strlen(key) > 256) {
        return -1;
    }

    sprintf(buff, "%s%s", key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

    SHA1Init(&ctx);
    SHA1Update(&ctx, buff, strlen(buff));
    SHA1Final(hash, &ctx);

    const char* base64 = b64_encode(hash, sizeof(hash));
    strncpy(b64buff, base64, len - 1);
    b64buff[len - 1] = '\0';
    free(base64);

    return 0;
}

// 处理HTTP协议升级为WebSocket协议的握手请求，握手成功返回0，失败返回-1
// 成功时该函数会通过initWsFrameStruct初始化client的wsFrame
int wsShakeHands(const char* recvBuff, int recvLen, Client* client) {

	#define HTTP_MAXLEN 1536
	#define HTTP_400 "HTTP/1.1 400 Bad Request\r\n\r\n"

	// HTTP握手包太长，掐了
	if(recvLen > HTTP_MAXLEN) {
		socketSend(client->socket, HTTP_400, strlen(HTTP_400));
		log("wsShakeHands", "Request too long");
		return -1;
	}

	// 找不到HTTP头结尾
	if(!strstr(recvBuff, "\r\n\r\n")) {
		log("wsShakeHands", "Incomplete HTTP header");
		return -1;
	}

	const char *keyPos, *keyPosEnd;

	if(
		!strcmp(recvBuff, "GET / HTTP/1.1\r\n")        ||
		!strstr(recvBuff, "Connection: ")		       ||
		!strstr(recvBuff, "Upgrade: websocket")        ||
		!strstr(recvBuff, "Sec-WebSocket-Version: 13") ||
		!(keyPos = strstr(recvBuff, "Sec-WebSocket-Key: "))
	) {
		socketSend(client->socket, HTTP_400, strlen(HTTP_400));
		log("wsShakeHands", "Missing required fields");
		return -1;
	}

	keyPos = strstr(keyPos, ": ");
	keyPos += 2;

	if((keyPosEnd = strstr(keyPos, "\r\n")) == NULL) {
		log("wsShakeHands", "Can't find keyPosEnd");
		return -1;
	}

	char keyBuff[128], acptBuff[128];

	// 获取Sec-WebSocket-Key
	sprintf(keyBuff, "%.*s", keyPosEnd - keyPos, keyPos);

	log("wsShakeHands", "Sec-WebSocket-Key is %s", keyBuff);

	// 获取Sec-WebSocket-Accept
	getSecWebSocketAcceptKey(keyBuff, acptBuff, sizeof(acptBuff));

	log("wsShakeHands", "Sec-WebSocket-Accept is %s", acptBuff);
	

	// 协议升级

	char resBuff[256];

	// 注：当前的CORS设置可能会导致安全问题
	// 注：响应中没有包含Sec-Websocket-Protocol头，代表不接受任何客户端请求的ws扩展
	const char resHeader[] = 
		"HTTP/1.1 101 ojbk\r\n"
		"Connection: Upgrade\r\n"
		"Upgrade: websocket\r\n"
		"Sec-WebSocket-Accept: %s\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"\r\n"
	;
	
	int resLen = sprintf(resBuff, resHeader, acptBuff);

	// Send data to the client
	int iSendResult = socketSend(client->socket, resBuff, resLen);

	client->protocol = websocketProtocol;
	initWsFrameStruct(&client->wsFrame);		// 初始化ws帧结构
	
	if(iSendResult == SOCKET_ERROR) {
		return -1;
	}
	
	log("wsShakeHands", "Bytes sent: %d", iSendResult);
	log("wsShakeHands", "WebSocket handshake succeeded");

	return 0;
}

void wsClientTextDataHandle(const char* payload, uint64_t payloadLen, Client* client) {

	char buff[1024];
	
	// 注意，payload的文本数据不是以\0结尾
	log("wsClientDataHandle","Payload data is %.*s", payloadLen > 128 ? 128 : (unsigned int)payloadLen, payload);

	char* parseEnd;

	cJSON *json = cJSON_ParseWithOpts(payload, &parseEnd, 0);

	if(json == NULL) {

		const char *error_ptr = cJSON_GetErrorPtr();

		if (error_ptr != NULL) {
			log("jsonParse", "Error before: %d", error_ptr - payload);
		}

		return;
	}

	// 公有字段
	const cJSON* id = cJSON_GetObjectItemCaseSensitive(json, "id");
	const cJSON* method = cJSON_GetObjectItemCaseSensitive(json, "method");
	const cJSON* params = cJSON_GetObjectItemCaseSensitive(json, "params");

	if(!cJSON_IsString(method)) {
		cJSON_Delete(json);
		return;
	}
	
	if(strcmp("sendMessage", method->valuestring) == 0) {

		const cJSON* type = cJSON_GetObjectItemCaseSensitive(params, "type");
		const cJSON* group = cJSON_GetObjectItemCaseSensitive(params, "group");
		const cJSON* qq = cJSON_GetObjectItemCaseSensitive(params, "qq");
		const cJSON* content = cJSON_GetObjectItemCaseSensitive(params, "content");

		if(
			cJSON_IsNumber(type) && cJSON_IsString(group) && 
			(cJSON_IsString(qq) || cJSON_IsString(content))
		) {
			log("jsonRPC", "Client call sendMessage method");
			char* gbkText = UTF8ToGBK(content->valuestring);
			QL_sendMessage(type->valueint, cJSON_IsString(content) ? group->valuestring : "", cJSON_IsString(qq) ? qq->valuestring : "", gbkText, authCode);
			free(gbkText);
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("withdrawMessage", method->valuestring) == 0) {

		const cJSON* group = cJSON_GetObjectItemCaseSensitive(params, "group");
		const cJSON* msgid = cJSON_GetObjectItemCaseSensitive(params, "msgid");

		if(cJSON_IsString(group) && cJSON_IsString(msgid)) {
			log("jsonRPC", "Client call withdrawMessage method");
			QL_withdrawMessage(group->valuestring, msgid->valuestring, authCode);
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("getFriendList", method->valuestring) == 0) {

		if(cJSON_IsString(id)) {
			log("jsonRPC", "Client call getFriendList method");
			cJSON* root = cJSON_CreateObject();
			cJSON_AddItemToObject(root, "id", cJSON_CreateString(id->valuestring));

			const char* friendList = GBKToUTF8(QL_getFriendList(authCode));
			cJSON_AddItemToObject(root, "result", cJSON_Parse(friendList));

			int* newLen;
			const char* jsonStr = cJSON_PrintUnformatted(root);
			const char* frame = convertToWebSocketFrame(jsonStr, frameType_text, strlen(jsonStr), &newLen);
			socketSend(client->socket, frame, newLen);

			cJSON_Delete(root);
			free(friendList);
			free(jsonStr);
			free(frame);
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("addFriend", method->valuestring) == 0) {

		const cJSON* qq = cJSON_GetObjectItemCaseSensitive(params, "qq");
		const cJSON* message = cJSON_GetObjectItemCaseSensitive(params, "message");

		if(cJSON_IsString(qq)) {
			log("jsonRPC", "Client call addFriend method");
			if(!cJSON_IsString(message)) {
				QL_addFriend(qq->valuestring, "", authCode);
			} else {
				const char* text = UTF8ToGBK(message->valuestring);
				QL_addFriend(qq->valuestring, text, authCode);
				free(text);
			}
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("deleteFriend", method->valuestring) == 0) {

		const cJSON* qq = cJSON_GetObjectItemCaseSensitive(params, "qq");

		if(cJSON_IsString(qq)) {
			log("jsonRPC", "Client call deleteFriend method");
			QL_deleteFriend(qq->valuestring, authCode);
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("getGroupList", method->valuestring) == 0) {

		if(cJSON_IsString(id)) {
			log("jsonRPC", "Client call getGroupList method");
			cJSON* root = cJSON_CreateObject();
			cJSON_AddItemToObject(root, "id", cJSON_CreateString(id->valuestring));

			const char* groupList = GBKToUTF8(QL_getGroupList(authCode));
			cJSON_AddItemToObject(root, "result", cJSON_Parse(groupList));

			int* newLen;
			const char* jsonStr = cJSON_PrintUnformatted(root);
			const char* frame = convertToWebSocketFrame(jsonStr, frameType_text, strlen(jsonStr), &newLen);
			socketSend(client->socket, frame, newLen);

			cJSON_Delete(root);
			free(groupList);
			free(jsonStr);
			free(frame);
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("getGroupMemberList", method->valuestring) == 0) {

		const cJSON* group = cJSON_GetObjectItemCaseSensitive(params, "group");

		if(cJSON_IsString(id) && cJSON_IsString(group)) {
			log("jsonRPC", "Client call getGroupMemberList method");
			cJSON* root = cJSON_CreateObject();
			cJSON_AddItemToObject(root, "id", cJSON_CreateString(id->valuestring));

			const char* groupMemberList = GBKToUTF8(QL_getGroupMemberList(group->valuestring, authCode));
			cJSON_AddItemToObject(root, "result", cJSON_Parse(groupMemberList));

			int* newLen;
			const char* jsonStr = cJSON_PrintUnformatted(root);
			const char* frame = convertToWebSocketFrame(jsonStr, frameType_text, strlen(jsonStr), &newLen);
			socketSend(client->socket, frame, newLen);

			cJSON_Delete(root);
			free(groupMemberList);
			free(jsonStr);
			free(frame);
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("addGroup", method->valuestring) == 0) {

		const cJSON* group = cJSON_GetObjectItemCaseSensitive(params, "group");
		const cJSON* message = cJSON_GetObjectItemCaseSensitive(params, "message");

		if(cJSON_IsString(group)) {
			log("jsonRPC", "Client call addGroup method");
			if(!cJSON_IsString(message)) {
				QL_addGroup(group->valuestring, "", authCode);
			} else {
				const char* text = UTF8ToGBK(message->valuestring);
				QL_addGroup(group->valuestring, text, authCode);
				free(text);
			}
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("quitGroup", method->valuestring) == 0) {

		const cJSON* group = cJSON_GetObjectItemCaseSensitive(params, "group");

		if(cJSON_IsString(group)) {
			log("jsonRPC", "Client call quitGroup method");
			QL_quitGroup(group->valuestring, authCode);
		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("getGroupCard", method->valuestring) == 0) {

		const cJSON* group = cJSON_GetObjectItemCaseSensitive(params, "group");
		const cJSON* qq = cJSON_GetObjectItemCaseSensitive(params, "qq");

		if(cJSON_IsString(id) && cJSON_IsString(group) && cJSON_IsString(qq)) {
			log("jsonRPC", "Client call getGroupCard method");

			cJSON* root = cJSON_CreateObject();
			cJSON_AddItemToObject(root, "id", cJSON_CreateString(id->valuestring));

			const char* groupCard = GBKToUTF8(QL_getGroupCard(group->valuestring, qq->valuestring, authCode));
			cJSON_AddItemToObject(root, "result", cJSON_CreateString(groupCard));

			int* newLen;
			const char* jsonStr = cJSON_PrintUnformatted(root);
			const char* frame = convertToWebSocketFrame(jsonStr, frameType_text, strlen(jsonStr), &newLen);
			socketSend(client->socket, frame, newLen);

			cJSON_Delete(root);
			free(groupCard);
			free(jsonStr);
			free(frame);

		} else {
			log("jsonParse", "Invalid data");
		}

	} else if (strcmp("uploadImage", method->valuestring) == 0) {

		const cJSON* type = cJSON_GetObjectItemCaseSensitive(params, "type");
		const cJSON* object = cJSON_GetObjectItemCaseSensitive(params, "object");
		const cJSON* data = cJSON_GetObjectItemCaseSensitive(params, "data");

		if(cJSON_IsString(id) && cJSON_IsNumber(type) && cJSON_IsString(object) && cJSON_IsString(data)) {
			log("jsonRPC", "Client call uploadImage method");

			cJSON* root = cJSON_CreateObject();
			cJSON_AddItemToObject(root, "id", cJSON_CreateString(id->valuestring));

			const char* guid = QL_uploadImage(type->valueint, object->valuestring, data->valuestring, authCode);
			cJSON_AddItemToObject(root, "result", cJSON_CreateString(guid));

			int* newLen;
			const char* jsonStr = cJSON_PrintUnformatted(root);
			const char* frame = convertToWebSocketFrame(jsonStr, frameType_text, strlen(jsonStr), &newLen);
			socketSend(client->socket, frame, newLen);

			cJSON_Delete(root);
			free(jsonStr);
			free(frame);

		} else {
			log("jsonParse", "Invalid data");
		}

	}

	cJSON_Delete(json);
}

// 处理WebSocket帧数据，返回-1代表需要关闭连接
int wsClientDataHandle(const char* recvBuff, int recvLen, Client* client) {

	WsFrame* wsFrame = &client->wsFrame;

	if(recvLen == 0) {
		return 0;
	}

	int consume = readWebSocketFrameStream(wsFrame, recvBuff, recvLen);

	log("wsClientDataHandle", "Consume %d bytes of data in %d bytes", consume, recvLen);
	log("wsClientDataHandle", "wsFrame->state is %d", wsFrame->state);

	if(wsFrame->state == frameState_success) {

		log("wsClientDataHandle", "Header and payload lengths are %llu and %llu", wsFrame->headerLen, wsFrame->payloadLen);

		// 暂时不处理多帧数据，遇到多帧数据关闭连接
		if(wsFrame->FIN == 0) {
			log("wsClientDataHandle", "This is not the final fragment in a message");
			return -1;
		}

		// 客户端希望关闭连接
		if(wsFrame->frameType == frameType_connectionClose) {
			log("wsClientDataHandle", "Connection close frame");
			return -1;
		}

		// 遇到意料之外的帧类型
		if( wsFrame->frameType == frameType_binary 		||
			wsFrame->frameType == frameType_pong		||
			wsFrame->frameType == frameType_continuation
		) {
			log("wsClientDataHandle", "Unexpected frame type");
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
			int newLen;
			const char* frame = convertToWebSocketFrame(payload, frameType_pong, payloadLen, &newLen);
			log("wsClientDataHandle", "pong");
			socketSend(client->socket, frame, newLen);
			free(frame);
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
		return wsClientDataHandle(recvBuff + consume, recvLen - consume, wsFrame);
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
				
				log("receiveComingData", "Bytes received: %d", iResult);
				
				if(clientSockets->clients[i].protocol == websocketProtocol) {
					int result = wsClientDataHandle(recvbuf, iResult, &clientSockets->clients[i]);
					if(result == -1) goto removeClientSocket;
				} else {
					int result = wsShakeHands(recvbuf, iResult, &clientSockets->clients[i]);
					if(result != 0) goto removeClientSocket;
				}

			} else {
				
				if(iResult == 0) {
					// 客户端礼貌的关闭连接 
					log("receiveComingData", "Connection closing...");
				} else {
					// 客户端异常关闭连接等情况
					log("receiveComingData", "Recv failed: %d", WSAGetLastError());
				}
				
				// 从数组中移除该socket并调用closesocket 
				removeClientSocket:
				
				// 该socket不处于数组末尾 
				if(i < clientSockets->total - 1) {
					
					clientSockets->clients[i] = 
						clientSockets->clients[--clientSockets->total];  // 将数组末尾的socket填到当前位置 
												
					i--;		// 回退循环计数，从当前位置继续循环 

				} else {
					clientSockets->total--;
					i--;
				}
				
				log("receiveComingData", "Now client sockets length: %d", clientSockets->total);
				
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
	SOCKET 	clientSocket;
    
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
			log("receiveConnect", "Threads will exit");
			TerminateThread(hHandle, NULL);		// 粗暴的终止receiveComingData线程 
			
			log("receiveConnect", "Closing all client sockets...");

			// 关闭所有客户端连接 
			for(int i = 0; i < clientSockets->total; i++) {
				closesocket(clientSockets->clients[i].socket);   	
			}
			clientSockets->total = 0;
			
			ExitThread(NULL); 	// 退出 
		}
		
		log("receiveConnect", "Accept failed: %d", WSAGetLastError());
		goto receivingConnectLoop;
	}
	
	log("receiveConnect", "Accepted client: %s:%d", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
	
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
    	log("ServerStart", "WSAStartup failed");
    	return -1;
    }
    
    struct sockaddr_in sockAddr;
    
    ZeroMemory(&sockAddr, sizeof(sockAddr));
    sockAddr.sin_family = PF_INET;
    sockAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    sockAddr.sin_port = htons(SERVER_PORT);
    
    serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(serverSocket == INVALID_SOCKET) {
		log("ServerStart", "Error at socket(): %d", WSAGetLastError());
    	WSACleanup();
    	return -1;
    }
	
    if(bind(serverSocket, (SOCKADDR*)&sockAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
        log("ServerStart","Bind failed with error: %d", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return -1;
    }
    
    if(listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
	    log("ServerStart", "Listen failed with error: %d", WSAGetLastError());
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
		log("Event_pluginStart", "WebSocket server startup failed");
	} else {
		log("Event_pluginStart", "WebSocket server startup success");
	}
	
	return 0;
} 

DllExport(int) Event_pluginStop(void) {
	
	serverStop();
	
	log("Event_pluginStop", "WebSocket server stopped"); 
	
	return 0;
}

DllExport(int) Event_GetNewMsg (
	int type,			// 1=好友消息 2=群消息 3=群临时消息 4=讨论组消息 5=讨论组临时消息 6=QQ临时消息
	const char* group, 	// 类型为1或6的时候，此参数为空字符串，其余情况下为群号或讨论组号
	const char* qq,		// 消息来源QQ号 "10000"都是来自系统的消息(比如某人被禁言或某人撤回消息等)
 	const char* msg,	// 消息内容
	const char* msgid	// 消息id，撤回消息的时候会用到，群消息会存在，其余情况下为空  
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

	size_t len;
	const char* frame = convertToWebSocketFrame(jsonStr, frameType_text, strlen(jsonStr), &len);

	for(int i = 0; i < clientSockets.total; i++) {
		socketSend(clientSockets.clients[i].socket, frame, len);
	}

	cJSON_Delete(root);
	free(u8Content);
	free(jsonStr);
	free(frame);

	return 0;	// 返回0下个插件继续处理该事件，返回1拦截此事件不让其他插件执行
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
	
	if(loadQQLightAPI() != 0) {
		MessageBox(NULL, "The message.dll load failed", "error", MB_OK);
		return FALSE;
	}
	
	/* Returns TRUE on success, FALSE on failure */
    return TRUE;
}
