#include <winsock2.h>
#include "ws.h"

#ifndef QLWS_SERVER_H

#define QLWS_SERVER_H

int wsFrameSend(SOCKET socket, const char* buff, int len, FrameType type);
void wsFrameSendToAll(const char* buff, int len, FrameType type);
int serverStart(void);
void serverStop(void);

#endif
