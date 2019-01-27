#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <windef.h>
#include <stdlib.h>
#include <winsock2.h>
#include "lib/sha1/sha1.h"
#include "lib/base64/b64.h"
#include "ws.h"

// 不区分大小写的比较字符串，相等返回true
bool stricasecmp(const char* a, const char* b) {
  do {
    if(*a == '\0' && *b == '\0')
      return true;
  } while(tolower(*a++) == tolower(*b++));

  return false;
}

// 不区分大小写的比较字符串，n个字符内（包括n）相等返回true
bool strnicasecmp(const char* a, const char* b, unsigned n) {
  do {
    if(n-- == 0 || (*a == '\0' && *b == '\0'))
      return true;
  } while(tolower(*a++) == tolower(*b++));

  return false;
}

// 打印日志函数声明
void pluginLog(const char* type, int level, const char* format, ...);

// 初始化帧结构
void initWsFrameStruct(WsFrame* wsFrame) {
    wsFrame->state = frameState_init;
    wsFrame->FIN = false;
    wsFrame->frameType = frameType_connectionClose;
    memset(wsFrame->mask, 0, sizeof(wsFrame->mask));
    wsFrame->buff = NULL;
    wsFrame->buffSize = 0;
    wsFrame->handledLen = 0;
    wsFrame->headerLen = 0;
    wsFrame->payloadLen = 0;
    wsFrame->next = NULL;
}

// 将数据转换为WebSocket帧并返回转换后内存空间，记得free
// len为数据长度，对于文本不包括\0，newLen返回帧长度
// type暂时只支持frameType_text、frameType_pong
char* convertToWebSocketFrame(const char* data, FrameType type, size_t len, size_t* newLen) {

    char* frame = malloc(len + 10);    // 服务器端帧头最多十字节

    if(type == frameType_text) {
        frame[0] = 0X81;
    } else if (type == frameType_pong) {
        frame[0] = 0X8A;
    } else {
        frame[0] = 0X88;    // type值在预料之外时发送关闭连接指令
    }

    if(len < 126) {

        frame[1] = len;

        memcpy(frame + 2, data, len);

        *newLen = len + 2;

        return frame;
        
    } else if (len >= 126 && len < 65536) {
        
        frame[1] = 126;
        frame[2] = (uint8_t)(len >> 8);    // 高位
        frame[3] = (uint8_t)len;            // 低位

        memcpy(frame + 4, data, len);

        *newLen = len + 4;

        return frame;

    } else if (len >= 65536) {

        frame[1] = 127;
        frame[2] = 0;
        frame[3] = 0;
        frame[4] = 0;
        frame[5] = 0;
        frame[6] = (uint8_t)(len >> 24);
        frame[7] = (uint8_t)(len >> 16);
        frame[8] = (uint8_t)(len >> 8);
        frame[9] = (uint8_t)len;

        memcpy(frame + 10, data, len);

        *newLen = len + 10;

        return frame;
    }
}

// 返回已读取的buff内的字节数，通常等于传入buff长度
// 如果完整读完一个frame后还有数据剩余或读取发生错误，就不会等于buff长度
// 当buff中的数据不足时，一次函数调用不能读取到一个完整的帧，可以之后再提供接下来的数据继续调用该函数组成一个完整的帧
// 调用该函数后通过wsFrame.state来判断读取状态
int readWebSocketFrameStream(WsFrame* wsFrame, const char* buff, int len) {

    // 申请足够容纳传入数据的空间，并拷贝传入数据

    if(wsFrame->buff == NULL) {

        wsFrame->buff = malloc(len);
        wsFrame->buffSize = len;

        memcpy(wsFrame->buff, buff, len);

    } else {

        char* copyStartAddr;
        int requiedLen = wsFrame->buffSize + len; 

        wsFrame->buff = realloc((void*)wsFrame->buff, requiedLen);
        copyStartAddr = wsFrame->buff + wsFrame->buffSize;
        wsFrame->buffSize = requiedLen;

        memcpy(copyStartAddr, buff, len);
    }
    

    // 消耗的数据量
    int consumed = 0;

    stateTransitionBegin:

    switch(wsFrame->state) {

        case frameState_init:

            if(wsFrame->buffSize < 1) {
                return consumed;
            }

            wsFrame->FIN = !!(wsFrame->buff[0] & 0X80);

            // RSV位不全为0，存在扩展协议，服务器不处理扩展协议
            if((wsFrame->buff[0] & 0X70) != 0) {
                wsFrame->state = frameState_failure;
                break;
            }

            int opcode = wsFrame->buff[0] & 0X0F;

            if(opcode == 0X0) {
                wsFrame->frameType = frameType_continuation;
            } else if(opcode == 0X1) {
                wsFrame->frameType = frameType_text;
            } else if(opcode == 0X2) {
                wsFrame->frameType = frameType_binary;
            } else if(opcode == 0X8) {
                wsFrame->frameType = frameType_connectionClose;
            } else if(opcode == 0X9) {
                wsFrame->frameType = frameType_ping;
            } else if(opcode == 0XA) {
                wsFrame->frameType = frameType_pong;
            } else {
                wsFrame->state = frameState_failure;
                break;
            }

            consumed += 1;
            wsFrame->handledLen += 1;
            wsFrame->headerLen += 1;
            wsFrame->state = frameState_firstByte;

        break;

        case frameState_firstByte:

            if(wsFrame->buffSize < 2) {
                return consumed;
            }

            // 标准规定客户端传入帧的掩码位必须不为0
            if((wsFrame->buff[1] & 0X80) == 0) {
                wsFrame->state = frameState_failure;
            }

            wsFrame->state = frameState_mask;

        break;

        case frameState_mask:

            if(wsFrame->buffSize < 2) {
                return consumed;
            }

            uint8_t payloadLen = wsFrame->buff[1] & 0X7F;

            // frame-payload-length-7
            if(payloadLen < 126) {
                wsFrame->payloadLen = payloadLen;
                wsFrame->state = frameState_7bitLength;
            } else if (payloadLen == 126) {
                wsFrame->state = frameState_16bitLengthWait;
            } else if (payloadLen == 127) {
                wsFrame->state = frameState_63bitLengthWait;
            }

            consumed += 1;
            wsFrame->headerLen += 1;
            wsFrame->handledLen += 1;

        break;

        case frameState_7bitLength:

            // 2字节共有字段 + 0字节附加长度字段 + 4字节掩码
            
            if(wsFrame->buffSize < 6) {
                return consumed;
            }

            for(int i = 0; i < 4; i++) {
                wsFrame->mask[i] = wsFrame->buff[i + 2];
            }

            consumed += 4;
            wsFrame->headerLen += 4;
            wsFrame->handledLen += 4;

            wsFrame->state = frameState_maskingKey;

        break;

        case frameState_16bitLengthWait:

            if(wsFrame->buffSize < 4) {
                return consumed;
            }

            wsFrame->payloadLen = ((uint16_t)wsFrame->buff[2] << 8) + (uint16_t)wsFrame->buff[3];

            consumed += 2;
            wsFrame->headerLen += 2;
            wsFrame->handledLen += 2;

            wsFrame->state = frameState_16bitLength;

        break;

        case frameState_63bitLengthWait:

            if(wsFrame->buffSize < 10) {
                return consumed;
            }

            unsigned char* recvBuff = wsFrame->buff;

            // 注：标准规定64位时最高bit必须为0，这里未作处理                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         
            wsFrame->payloadLen = 
                ((uint64_t)recvBuff[2] << (8 * 7)) +
                ((uint64_t)recvBuff[3] << (8 * 6)) +
                ((uint64_t)recvBuff[4] << (8 * 5)) +
                ((uint64_t)recvBuff[5] << (8 * 4)) +
                ((uint64_t)recvBuff[6] << (8 * 3)) +
                ((uint64_t)recvBuff[7] << (8 * 2)) +
                ((uint64_t)recvBuff[8] << (8 * 1)) +
                ((uint64_t)recvBuff[9] << (8 * 0));

            consumed += 8;
            wsFrame->headerLen += 8;
            wsFrame->handledLen += 8;

            wsFrame->state = frameState_63bitLength;

        break;

        case frameState_16bitLength:

            // 2字节共有字段 + 2字节附加长度字段 + 4字节掩码

            if(wsFrame->buffSize < 8) {
                return consumed;
            }

            for(int i = 0; i < 4; i++) {
                wsFrame->mask[i] = wsFrame->buff[i + 4];
            }

            consumed += 4;
            wsFrame->headerLen += 4;
            wsFrame->handledLen += 4;

            wsFrame->state = frameState_maskingKey;

        break;

        case frameState_63bitLength:

            // 2字节共有字段 + 8字节附加长度字段 + 4字节掩码

            if(wsFrame->buffSize < 14) {
                return consumed;
            }

            for(int i = 0; i < 4; i++) {
                wsFrame->mask[i] = wsFrame->buff[i + 10];
            }

            consumed += 4;
            wsFrame->headerLen += 4;
            wsFrame->handledLen += 4;

            wsFrame->state = frameState_maskingKey;

        break;

        case frameState_maskingKey:

            wsFrame->state = frameState_readingData;
    
        break;

        case frameState_readingData:
        
            ;    // case第一个语句不能是变量声明
            uint64_t total = wsFrame->payloadLen + wsFrame->headerLen;

            // 注意 buff的长度可能大于帧总长度
            // 因为TCP是面向字节流的，buff中有可能包含下一帧的数据
            // 所以读取时要根据帧头和载荷长度来判断最多读多少数据
            if(wsFrame->buffSize >= total) {
                consumed += total - wsFrame->handledLen;
                wsFrame->handledLen = total;
                wsFrame->state = frameState_success;
            } else {
                consumed += wsFrame->buffSize - wsFrame->handledLen;
                wsFrame->handledLen = wsFrame->buffSize;
                return consumed;
            }

        break;

        case frameState_success:

            return consumed;

        break;

        case frameState_failure:

            return consumed;

        break;
    }

    goto stateTransitionBegin;

    return consumed;
}

// 调用readWebSocketFrameStream后通过该函数释放申请内存
// 同时会重置帧结构及确保wsFrame.buff指向NULL
void freeWebSocketFrame(WsFrame* wsFrame) {
    if(wsFrame->buff) {
        free(wsFrame->buff);
    }
    initWsFrameStruct(wsFrame);
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
    free((void*)base64);

    return 0;
}

// 校验WebSocket握手的HTTP头，失败返回NULL，校验成功顺带返回Sec-WebSocket-Key，记得free
char* verifyHandshakeHeaders(const char* str, size_t len) {

    char* secKey = NULL;
    char a[len + 1], b[len + 1];
    bool connection, upgrade, version, key;
    connection = upgrade = version = key = false;

    if(strcmp(str + len - 4, "\r\n\r\n") != 0) {
        pluginLog("verifyHandshakeHeaders", 1, "HTTP header does not end with '\\r\\n\\r\\n'");
        return NULL;
    }
    
    const char* cur1 = strstr(str, "\r\n") + 2;
    const char* cur2;

    while((cur2 = strstr(cur1, "\r\n")) != cur1) {

        cur2 += 2;  // 跳过\r\n

        const char* colon = strchr(cur1, ':');
        if(colon == NULL || colon >= cur2) {
            pluginLog("verifyHandshakeHeaders", 1, "Unexpected HTTP header");
            break;
        }

        if(sscanf(cur1, "%[^:]:%s", a, b) != 2) {
            pluginLog("verifyHandshakeHeaders", 1, "HTTP header parsing failed");
            break;
        }

        if(stricasecmp(a, "connection")) {

            connection = true;

        } else if (stricasecmp(a, "upgrade")) {

            if(!stricasecmp(b, "websocket")) {
                pluginLog("verifyHandshakeHeaders", 1, "Unexpected value '%s' of Upgrade filed", b);
                break;
            }

            upgrade = true;

        } else if (stricasecmp(a, "Sec-WebSocket-Version")) {

            if(!stricasecmp(b, "13")) {
                pluginLog("verifyHandshakeHeaders", 1, "Unexpected value '%s' of Sec-WebSocket-Version filed", b);
                break;
            }

            version = true;

        } else if (stricasecmp(a, "Sec-WebSocket-Key")) {

            if(!key) {
                key = true;
                secKey = malloc(strlen(b) + 1);
                strcpy(secKey, b);
            }
        }

        cur1 = cur2;
    }

    if(!(connection && upgrade && version && key)) {
        pluginLog("verifyHandshakeHeaders", 1, "Missing necessary fields");
        if(key) free((void*)secKey);       // 释放申请的内存
        return NULL;
    }

    return secKey;
}

// 处理HTTP协议升级为WebSocket协议的握手请求，握手成功返回0，失败返回-1
int wsShakeHands(const char* recvBuff, int recvLen, SOCKET socket, const char* path) {

    #define HTTP_MAXLEN 1536
    #define HTTP_400 "HTTP/1.1 400 Bad Request\r\n\r\n"

    // HTTP握手包太长，掐了
    if(recvLen > HTTP_MAXLEN) {
        send(socket, HTTP_400, strlen(HTTP_400), 0);
        pluginLog("wsShakeHands", 1, "Request too long");
        return -1;
    }

    // 注：recvBuff不以'\0'结尾
    char resText[recvLen + 1];
    memcpy(resText, recvBuff, recvLen);
    resText[recvLen] = '\0';

    char requestLine[512];
    sprintf(requestLine, "GET %s%s HTTP/1.1\r\n", (strlen(path) == 0 || path[0] != '/') ? "/" : "", path);

    // 注：路径部分也被不区分大小写的比较
    if(!strnicasecmp(resText, requestLine, strlen(requestLine))) {
        send(socket, HTTP_400, strlen(HTTP_400), 0);
        pluginLog("wsShakeHands", 1, "Unexpected request line");
        return -1;
    }

    const char *secKey = verifyHandshakeHeaders(resText, recvLen);

    if(!secKey) {
        send(socket, HTTP_400, strlen(HTTP_400), 0);
        return -1;
    }

    // 获取Sec-WebSocket-Accept
    char acptBuff[128];
    getSecWebSocketAcceptKey(secKey, acptBuff, sizeof(acptBuff));
    pluginLog("wsShakeHands", 0, "Sec-WebSocket-Key is '%s'", secKey);
    pluginLog("wsShakeHands", 0, "Sec-WebSocket-Accept is '%s'", acptBuff);
    free((void*)secKey);   // 释放secKey

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
    int iSendResult = send(socket, resBuff, resLen, 0);
    
    if(iSendResult == SOCKET_ERROR) {
        return -1;
    }
    
    pluginLog("wsShakeHands", 0, "Bytes sent: %d", iSendResult);
    pluginLog("wsShakeHands", 1, "WebSocket handshake succeeded");

    return 0;
}
