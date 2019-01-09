#include <winsock2.h>
#include "ws.h"

#ifndef QLWS_SERVER_H

#define QLWS_SERVER_H

int wsFrameSend(SOCKET socket, const char* buff, int len, FrameType type);
void wsFrameSendToAll(const char* buff, int len, FrameType type);
int serverStart(const char* address, u_short port, const char* path);
void serverStop(void);

#endif
