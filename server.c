#include <windows.h>
#include <winsock2.h>
#include "ws.h"
#include "server.h"

typedef enum {
    socketProtocol,
    websocketProtocol
} Protocol;

typedef struct {
    Protocol protocol;
    SOCKET socket;
    WsFrame wsFrame;    // 仅在升级协议后使用
} Client;

// 回调函数
void wsClientTextDataHandle(const char* payload, uint64_t payloadLen, SOCKET socket);

// 打印日志函数声明
void pluginLog(const char* type, const char* format, ...);

#define MAX_CLIENT_NUM FD_SETSIZE
static struct {
    int    total;
    Client clients[MAX_CLIENT_NUM];
} clientSockets;

static SOCKET serverSocket;

// 将数据转换成WebSocket帧并发送
// 需要调用者自己确保socket已完成WebSocket握手
int wsFrameSend(SOCKET socket, const char* buff, int len, FrameType type) {

    int newLen;
    const char* frame = convertToWebSocketFrame(buff, type, len, &newLen);

    int iSendResult = send(socket, frame, newLen, 0);

    if(iSendResult == SOCKET_ERROR) {
        pluginLog("wsFrameSend", "Send failed: %d", WSAGetLastError());
        goto wsFrameSendEnd;
    }

    pluginLog("wsFrameSend", "Bytes sent: %d", iSendResult);
    
    wsFrameSendEnd:    
    free((void*)frame);
    return iSendResult;
}

// 将数据转换为WebSocket帧并发送给所有已完成WebSocket握手的客户端
void wsFrameSendToAll(const char* buff, int len,  FrameType type) {
    for(int i = 0; i < clientSockets.total; i++) {
        if(clientSockets.clients[i].protocol == websocketProtocol) {
            pluginLog("wsFrameSendToAll", "Send data to %dst client", i);
            wsFrameSend(clientSockets.clients[i].socket, buff, len, type);
        }
    }
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
            wsClientTextDataHandle(payload, payloadLen, client->socket);
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

// 从客户数组中移除指定位置的客户，并关闭连接
// 如果被移除的客户不在数组末尾，数组末尾的客户会移动到被移除的客户所在位置
// 所以如果调用该函数时正在遍历客户数组，记得回退遍历位置
void removeClient(int pos) {

    SOCKET socket = clientSockets.clients[pos].socket;  // 保存需要被关闭的socket

    if(pos < clientSockets.total - 1) {      // 该socket不处于数组末尾 
        // 将数组末尾的socket填到当前位置 
        clientSockets.clients[pos] = clientSockets.clients[clientSockets.total - 1];
    }
    clientSockets.total--;

    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 1;
    setsockopt(socket, SOL_SOCKET, SO_LINGER, (const char*)&so_linger, sizeof(so_linger));
    closesocket(socket);

    pluginLog("removeClient", "Client socket closed, now length of clients: %d", clientSockets.total);
}

void receiveConnect(void) {
    
    SOCKET  clientSocket;
    SOCKADDR_IN client; 
    
    // Accept a connection   
    clientSocket = accept(serverSocket, (struct sockaddr*)&client, NULL);   

    // 当连接数达到上限时拒绝连接
    if(clientSockets.total >= MAX_CLIENT_NUM) {
        closesocket(clientSocket);
        return;
    }
    
    if(clientSocket != INVALID_SOCKET) {

        pluginLog("receiveConnect", "Accepted client: %s:%d", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        
        clientSockets.clients[clientSockets.total].socket = clientSocket;
        clientSockets.clients[clientSockets.total].protocol = socketProtocol;
        clientSockets.total++;

        return;
    }

    int errCode = WSAGetLastError();
    
    // serverSocket不是一个套接字，即已经调用了serverStop，执行了closesocket(serverSocket)
    if(errCode == WSAENOTSOCK) {
        
        pluginLog("receiveConnect", "Closing all client sockets...");

        // 关闭所有客户端连接 
        for(int i = 0; i < clientSockets.total; i++) {
            closesocket(clientSockets.clients[i].socket);       
        }
        clientSockets.total = 0;
        
        pluginLog("receiveConnect", "Threads will exit");
        ExitThread(0);     // 退出 
    }
    
    pluginLog("receiveConnect", "Accept failed: %d", errCode);
}

void receiveComingData(void) {

    #define RECV_BUFLEN 0X40000

    char recvbuf[RECV_BUFLEN];
    int iResult;

    int ret; 
    fd_set fdread; 
    struct timeval tv = {1, 0}; 
 
    receivingDataLoop:
        
    FD_ZERO(&fdread); 
    FD_SET(serverSocket, &fdread);
    for(int i = 0; i < clientSockets.total; i++) {     
        FD_SET(clientSockets.clients[i].socket, &fdread);   
    }
    
    ret = select(0, &fdread, NULL, NULL, &tv);
    
    if(ret == 0) {
        goto receivingDataLoop;     // select的等待时间到达，开始下一轮等待 
    }

    if(FD_ISSET(serverSocket, &fdread)) {
        receiveConnect();
    }
    
    for(int i = 0; i < clientSockets.total; i++) {

        Client* client = &clientSockets.clients[i];
        
        if(!FD_ISSET(client->socket, &fdread)) {
            continue;
        }

        iResult = recv(client->socket, recvbuf, RECV_BUFLEN, 0);

        if(iResult > 0) {
            
            pluginLog("receiveComingData", "Bytes received: %d", iResult);
            
            // 协议升级
            if(client->protocol == socketProtocol) {
                int result = wsShakeHands(recvbuf, iResult, client->socket);
                if(result != 0) {
                    removeClient(i--);
                } else {
                    client->protocol = websocketProtocol;
                    initWsFrameStruct(&client->wsFrame);        // 初始化ws帧结构
                }
            }
            // WebSocket通信
            else if(client->protocol == websocketProtocol) {
                int result = wsClientDataHandle(recvbuf, iResult, client);
                if(result == -1) {
                    removeClient(i--);
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
            
            removeClient(i--);
        }
    }

    goto receivingDataLoop;
}

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

    clientSockets.total = 0;
    
    DWORD dwThreadId;
    HANDLE hHandle = CreateThread(NULL, 0, (void*)receiveComingData, NULL, 0, &dwThreadId);
    
    return 0;
}

void serverStop(void) {
    closesocket(serverSocket);
    WSACleanup();
}
