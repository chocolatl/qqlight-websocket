# QQLight-WebSocket

QQLight机器人（原Amanda QQ）接口的WebSocket协议实现

让你能通过WebSocket协议使用任何喜欢的语言使用QQLight接口编写QQ机器人程序

## 使用

插件启动后监听`49632`端口，在本机可以使用WebSocket协议通过URL`ws://localhost:49632/`连接

当新`事件`发生，插件服务器会主动推送到所有已连接的客户端，服务器推送的事件列表可查看API文档中列出的`事件`

连接建立后，可以随时发送支持的`接口`消息给服务器，实现发送QQ消息等功能，接口列表可查看API文档中列出的`接口`

## 示例

### 浏览器示例

这是一个复读机（Echo）的Javascript示例，可以直接运行在浏览器上：

```js
var ws = new WebSocket('ws://localhost:49632');
ws.onmessage = function(ev) {
    var data = JSON.parse(ev.data);
    var params = data.params;
    console.log(data);
    if(data.event === 'message') {
        var rpc = {
            method: "sendMessage",
            params: {
                type    : params.type,
                group   : params.group,
                qq      : params.qq,
                content : params.content
            }
        };
        ws.send(JSON.stringify(rpc));
    }
}
```

### Node.js示例

这是一个复读机（Echo）的Node.js示例，使用了[ws](https://github.com/websockets/ws)模块：

```js
const WebSocket = require('ws');
const ws = new WebSocket('ws://localhost:49632/');
ws.on('message', data => {
    data = JSON.parse(data);
    if(data.event === 'message') {
        ws.send(JSON.stringify({
            method: 'sendMessage',
            params: {...data.params}
        }));
    }
});
```

## API文档

远程调用采用类似JSON-RPC的数据格式，服务端与客户端发送的消息都**必须**是顶层结构为对象的JSON格式文本，编码**必须**为UTF-8

如果调用的接口会返回数据，那么**必须**携带字符串类型的`id`字段，且每次调用都应该使用不同的`id`值，服务器返回结果会携带与调用时相同的`id`

为什么需要`id`？

对于网络I/O环境下，发送和接收通常不是一个同步的过程，发送数据未必按序到达，不同的`id`能帮助你辨别返回的数据属于哪次调用

### 事件.收到消息

```js
{
    "event": "message", 
    "params": {
        "type"    : 2,   // 1=好友消息、2=群消息、3=群临时消息、4=讨论组消息、5=讨论组临时消息、6=QQ临时消息
        "group"   : "",  // 类型为1或6的时候，此参数为空字符串，其余情况下为群号或讨论组号
        "qq"      : "",  // 消息来源QQ号，"10000"都是来自系统的消息（比如某人被禁言或某人撤回消息等）
        "content" : "",  // 消息内容
        "msgid"   : ""   // 消息id，撤回消息的时候会用到，群消息会存在，其余情况下为空  
    }
}
```

### 接口.发送消息

```js
{
    "method": "sendMessage", 
    "params": {
        "type"    : 2,
        "group"   : "",
        "qq"      : "",
        "content" : ""
    }
}
```

无返回值

### 接口.撤回消息

```js
{
    "method": "withdrawMessage", 
    "params": {
        "group": "",
        "msgid": ""
    }
}
```

无返回值

### 接口.获取好友列表

```js
{
    "id": "",
    "method": "getFriendList"
}
```

### 接口.添加好友

```js
{
    "method": "addFriend", 
    "params": {
        "qq": "",
        "message": ""       // 验证消息，可选
    }
}
```

无返回值

### 接口.删除好友

```js
{
    "method": "deleteFriend", 
    "params": {
        "qq": ""
    }
}
```

无返回值

### 接口.获取群列表

```js
{
    "method": "getGroupList"
}
```

### 接口.获取群成员列表

```js
{
    "method": "getGroupMemberList",
    "params": {
        "group": ""
    }
}
```

### 接口.添加群

```js
{
    "method": "addGroup", 
    "params": {
        "group": "",
        "message": ""
    }
}
```

无返回值

### 接口.退出群

```js
{
    "method": "quitGroup", 
    "params": {
        "group": ""
    }
}
```

无返回值

### 接口.获取群名片

```js
{
    "method": "getGroupCard", 
    "params": {
        "group": "",
        "qq": ""
    }
}
```

### 替换符.at

在发送的群消息中使用`[QQ:at=xxx]`表示at某个群成员，其中`xxx`可以替换为任意群成员QQ

使用`[QQ:at=all]`表示at全体成员，但仅在发送者为群管理员时有效

### 替换符.face

在发送的消息中使用`[QQ:face=212]`代表一个表情，其中`212`为表情代码，可以自己通过尝试获得更多表情代码


## 安全性

1. 程序不会进行身份认证，意味着任何人都能通过WebSocket协议调用接口，如果你的QQ机器人部署在公网环境，请通过防火墙关闭插件使用的服务端口

2. 程序假设使用者是准守规则的合法用户，所以不接受存在安全漏洞的设定，非法的请求可能导致程序崩溃或安全问题。如果你的QQ机器人部署在公网环境，请通过防火墙关闭插件使用的服务端口

3. 总之你应该通过防火墙关闭服务端口，如果希望能远程调用，可以通过实现一个包含认证的WebSocket Server/Client作为代理服务器

4. 插件服务器通过`CORS`设置允许所有站点访问服务器，如果你在服务器上通过浏览器浏览包含针对该插件的恶意代码时可能被攻击

## 许可证

```
GLWT（祝你好运）公共许可证
版权所有（C）每个人，除了作者

任何人都被允许复制、分发、修改、合并、销售、出版、再授权或
任何其它操作，但风险自负。

作者对这个项目中的代码一无所知。
代码处于可用或不可用状态，没有第三种情况。


                祝你好运公共许可证
            复制、分发和修改的条款和条件

0：在不导致作者被指责或承担责任的情况下，你可以做任何你想
要做的事情。

无论是在合同行为、侵权行为或其它因使用本软件产生的情形，作
者不对任何索赔、损害承担责任。

祝你好运及一帆风顺
```
