#include <stdint.h>

typedef enum FrameType {
    frameType_continuation,
    frameType_text,
    frameType_binary,
    frameType_connectionClose,
    frameType_ping,
    frameType_pong
} FrameType;

typedef enum FrameState {
    frameState_init,            // 未读取任何字节
    frameState_firstByte,       // 已读取首字节FIN、RSV、opcode
    frameState_mask,            // 已读取掩码
    frameState_7bitLength,      // 已读取7bit长度
    frameState_16bitLengthWait, // 等待读取16bit长度
    frameState_63bitLengthWait, // 等待读取63bit长度
    frameState_16bitLength,     // 已读取16bit长度
    frameState_63bitLength,    	// 已读取63bit长度
    frameState_maskingKey,      // 已读取Masking-key
    frameState_readingData,     // 正在读取载荷数据
    frameState_success,         // 读取完毕
    frameState_failure          // 读取错误
} FrameState;

typedef struct WsFrame {
    FrameState state;
    bool FIN;
    FrameType frameType;
    uint8_t  mask[4];
    unsigned char*  buff;   // 数据存放的空间
    uint64_t buffSize;      // 当前申请的buff大小
    uint64_t handledLen;    // 已处理的帧长度
    uint64_t headerLen;     // 帧头长度 只有在state为'已读取掩码'及之后才有意义
    uint64_t payloadLen;    // 载荷长度 只有在state为'已读取xbit长度'后才有意义
    struct WsFrame* next;   // 下一帧的指针
} WsFrame;

void initWsFrameStruct(WsFrame* wsFrame);
char* convertToWebSocketFrame(const char* data, FrameType type, size_t len, size_t* newLen);
int readWebSocketFrameStream(WsFrame* wsFrame, const char* buff, int len);
void freeWebSocketFrame(WsFrame* wsFrame);
