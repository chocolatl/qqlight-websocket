#include <stdbool.h>
#include <windef.h>
#include "wsFrame.h"

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

	char* frame = malloc(len + 10);	// 服务器端帧头最多十字节

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
		frame[2] = (uint8_t)(len >> 8);	// 高位
		frame[3] = (uint8_t)len;			// 低位

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

		wsFrame->buff = realloc(wsFrame->buff, requiedLen);
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
		
			;	// case第一个语句不能是变量声明
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