dllname = websocket.protocol.plugin

$(dllname).dll: main.c server.o ws.o api.o cjson.o sha1.o b64_encode.o b64_decode.o
	gcc -o $(dllname).o main.c -c -std=c99
	gcc -Wl,-add-stdcall-alias -shared -o $(dllname).dll $(dllname).o server.o api.o ws.o cjson.o sha1.o b64_encode.o b64_decode.o -lws2_32
	del *.o
	copy "./$(dllname).dll" "../QQLight/plugin/"
# -Wl,-add-stdcall-alias告诉链接器同时生成不带@n的导出函数名，QQLight需要不带@n的导出函数名

server.o: server.c server.h
	gcc -o server.o server.c -c -std=c99

ws.o: ws.c ws.h
	gcc -o ws.o ws.c -c -std=c99

api.o: api.c api.h
	gcc -o api.o api.c -c -std=c99 -w

cjson.o: lib/cjson/cJSON.c
	gcc -O2 -o cjson.o lib/cjson/cJSON.c -c

sha1.o: lib/sha1/sha1.c
	gcc -O2 -o sha1.o lib/sha1/sha1.c -c

b64_encode.o: lib/base64/encode.c
	gcc -O2 -o b64_encode.o lib/base64/encode.c -c

b64_decode.o: lib/base64/decode.c
	gcc -O2 -o b64_decode.o lib/base64/decode.c -c
	