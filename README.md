[![stars](https://img.shields.io/github/stars/Chocolatl/qqlight-websocket.svg)](https://github.com/Chocolatl/qqlight-websocket/stargazers)
[![download](https://img.shields.io/github/downloads/Chocolatl/qqlight-websocket/total.svg)](https://github.com/Chocolatl/qqlight-websocket/releases)
[![release](https://img.shields.io/github/release/Chocolatl/qqlight-websocket.svg)](https://github.com/Chocolatl/qqlight-websocket/releases)
[![license](https://img.shields.io/badge/license-GLWT-green.svg)](https://github.com/Chocolatl/qqlight-websocket/blob/master/LICENSE)

[QQLight机器人](http://www.52chat.cc/)（原Amanda QQ）框架的WebSocket-RPC插件

插件通过WebSocket与JSON实现远程过程调用，让你能使用任何喜欢的语言编写QQ机器人程序

## 使用方法

将插件复制到QQLight机器人框架的`plugin`目录中，运行QQLight机器人并在插件管理中启用插件

插件启动后默认监听`49632`端口，在本机可以使用WebSocket客户端通过URL`ws://localhost:49632/`连接服务器

### 配置

第一次启动插件后会在插件数据目录`plugin/websocket.protocol`中生成`config.json`文件，里面提供了一些插件配置项

你可以修改这些插件配置项并保存文件，然后重新启动插件使新的配置生效

#### address

服务器监听地址，默认为`127.0.0.1`，即只允许本机连接服务器，如果你希望通过外网连接服务器，可以设置为`0.0.0.0`

#### port

服务器监听端口号，默认为`49632`

#### path

WebSocket握手时的URL路径，默认为根路径`/`，即通过`ws://localhost:49632/`连接服务器。如果将路径修改为`/xxx/yyy`，则需要通过`ws://localhost:49632/xxx/yyy`才能连接服务器

路径应该只包含`字母`、`数字`及`/`，当允许通过外网连接服务器时，请设置一个足够复杂的路径，防止被他人恶意连接

## 示例

### 浏览器示例

这是一个复读机（Echo）的Javascript示例，可以直接运行在浏览器上：

```js
var ws = new WebSocket('ws://localhost:49632/');
ws.onmessage = function(ev) {
    var data   = JSON.parse(ev.data);
    var params = data.params;
    if(data.event === 'message') {
        var rpc = {
            id: Math.random().toString(),
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
            id: Math.random().toString(),
            method: 'sendMessage',
            params: {...data.params}
        }));
    }
});
```

## API文档

服务端与客户端发送的消息都是顶层结构为`对象`的JSON格式文本，文本编码为UTF-8

### 事件

`事件`是服务器会主动发送给客户端的消息，如机器人收到好友请求时，服务器会向客户端发送如下格式的消息：

```js
{
    "event": "friendRequest", 
    "params": {
        "qq"      : "123456",
        "message" : ""
    }
}
```

### 接口

`接口`是客户端可以发送给服务器的消息，服务器收到消息会调用机器人相应的方法处理，下面是删除好友的消息示例：

```js
{
    "id"    : "1024"
    "method": "deleteFriend", 
    "params": {
        "qq": ""
    }
}
```

所有`接口`调用必须携带`字符串`类型的`id`字段，且每次请求都应该使用不同的`id`，服务器返回结果会包含与调用时相同的`id`。`id`用于分辨返回的数据属于哪个调用

### 接口返回

无返回值的接口调用成功会返回仅包含`id`字段的对象：

```js
{
    "id"    : "1024"
}
```

有返回值的接口调用成功会返回包含`id`与`result`字段的对象，其中`result`字段类型与值视具体接口而定：

```js
{
    "id"    : "1024"
    "result": true
}
```

接口调用发生错误会返回包含`id`与`error`字段的对象，其中`error`字段为字符串类型的错误信息：

```js
{
    "id"   : "1024"
    "error": "Unknown Method"
}
```

### API列表

- [事件.收到消息](#事件收到消息)
- [事件.收到好友请求](#事件收到好友请求)
- [事件.成为好友](#事件成为好友)
- [事件.群成员增加](#事件群成员增加)
- [事件.群成员减少](#事件群成员减少)
- [事件.群管理员变动](#事件群管理员变动)
- [事件.加群请求](#事件加群请求)
- [事件.收款](#事件收款)
- [事件.Cookies更新](#事件Cookies更新)
- [接口.发送消息](#接口发送消息)
- [接口.撤回消息](#接口撤回消息)
- [接口.获取好友列表](#接口获取好友列表)
- [接口.添加好友](#接口添加好友)
- [接口.删除好友](#接口删除好友)
- [接口.获取群列表](#接口获取群列表)
- [接口.获取群成员列表](#接口获取群成员列表)
- [接口.添加群](#接口添加群)
- [接口.退出群](#接口退出群)
- [接口.获取群名片](#接口获取群名片)
- [接口.上传图片](#接口上传图片)
- [接口.获取QQ资料](#接口获取QQ资料)
- [接口.获取群资料](#接口获取群资料)
- [接口.邀请好友入群](#接口邀请好友入群)
- [接口.设置群名片](#接口设置群名片)
- [接口.获取当前登录账号](#接口获取当前登录账号)
- [接口.设置个性签名](#接口设置个性签名)
- [接口.获取QQ昵称](#接口获取QQ昵称)
- [接口.获取名片点赞数量](#接口获取名片点赞数量)
- [接口.点赞名片](#接口点赞名片)
- [接口.处理好友请求](#接口处理好友请求)
- [接口.设置在线状态](#接口设置在线状态)
- [接口.处理加群请求](#接口处理加群请求)
- [接口.移除群成员](#接口移除群成员)
- [接口.禁言](#接口禁言)
- [接口.全体禁言](#接口全体禁言)
- [接口.获取Cookies](#接口获取Cookies)
- [接口.获取Bkn](#接口获取Bkn)
- [接口.获取ClientKey](#接口获取ClientKey)
- [替换符.at](#替换符at)
- [替换符.face/emoji](#替换符faceemoji)
- [替换符.image/flash](#替换符imageflash)

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

### 事件.收到好友请求

```js
{
    "event": "friendRequest", 
    "params": {
        "qq"      : "",
        "message" : ""      // 验证消息
    }
}
```

收到该事件时可以通过调用[接口.处理好友请求](#接口处理好友请求)处理

### 事件.成为好友

```js
{
    "event": "becomeFriends", 
    "params": {
        "qq": ""
    }
}
```

### 事件.群成员增加

```js
{
    "event": "groupMemberIncrease", 
    "params": {
        "type"      : 1,        // 1=主动加群、2=被管理员邀请
        "group"     : "",       //
        "qq"        : "",       //
        "operator"  : ""        // 操作者QQ
    }
}
```

### 事件.群成员减少

```js
{
    "event": "groupMemberDecrease", 
    "params": {
        "type"      : 1,        // 1=主动退群、2=被管理员踢出
        "group"     : "",       //
        "qq"        : "",       //
        "operator"  : ""        // 操作者QQ，仅在被管理员踢出时存在
    }
}
```

### 事件.群管理员变动

```js
{
    "event": "adminChange", 
    "params": {
        "type"      : 1,        // 1=成为管理 2=被解除管理
        "group"     : "",       //
        "qq"        : ""        //
    }
}
```

### 事件.加群请求

```js
{
    "event": "groupRequest", 
    "params": {
        "type"      : 1,        // 1=主动加群、2=被邀请进群、3=机器人被邀请进群
        "group"     : "",       //
        "qq"        : "",       //
        "operator"  : "",       // 邀请者QQ，主动加群时不存在
        "message"   : "",       // 加群附加消息，只有主动加群时存在
        "seq"       : ""        // 序列号，处理加群请求时需要用到
    }
}
```

type为`1`时，指有人主动申请进群，这时`operator`字段为空，如果你是管理员，可以调用[接口.处理加群请求](#接口处理加群请求)处理

type为`2`时，指群员邀请别人进群，这时`message`字段为空，如果你是管理员，可以调用[接口.处理加群请求](#接口处理加群请求)处理

type为`3`时，指机器人被邀请进群，这时`message`字段为空，可以调用[接口.处理加群请求](#接口处理加群请求)处理

### 事件.收款

```js
{
    "event": "receiveMoney", 
    "params": {
        "type"      : 1,        // 1=好友转账、2=群临时会话转账、3=讨论组临时会话转账
        "group"     : "",       // type为1时此参数为空，type为2、3时分别为群号或讨论组号
        "qq"        : "",       // 转账者QQ
        "amount"    : "",       // 转账金额
        "message"   : "",       // 转账备注消息
        "id"        : ""        // 转账订单号
    }
}
```

### 事件.Cookies更新

```js
{
    "event": "updateCookies"
}
```

### 接口.发送消息

```js
{
    "method": "sendMessage", 
    "params": {
        "type"    : 2,      // 1=好友消息、2=群消息、3=群临时消息、4=讨论组消息、5=讨论组临时消息、6=QQ临时消息
        "group"   : "",     // 群号或讨论组号，发送消息给好友的情况下忽略
        "qq"      : "",     // QQ号，发送消息给群或讨论组的情况下忽略
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
    "id"     : "",
    "method" : "getFriendList"
}
```

### 接口.添加好友

```js
{
    "method": "addFriend", 
    "params": {
        "qq"        : "",
        "message"   : ""       // 验证消息，可选
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
        "group"     : "",
        "message"   : ""    // 验证消息，可选
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
        "group" : "",
        "qq"    : ""
    }
}
```

### 接口.上传图片

```js
{
    "method": "uploadImage",
    "params": {
        "type"  : 2,        // 1=私聊类型的图片、2=群组类型的图片
        "object": ""        // 图片准备发送到的QQ号或群组号
        "data"  : ""        // 图像数据转换的十六进制字符串
    }
}
```

该接口并不发送图片，而是将图片上传到QQ服务器，并返回`GUID`，`GUID`用法参见[替换符.image/flash](#替换符imageflash)

所获得的`GUID`只能对`type`和`object`指定的对象使用，否则图片可能无法显示

### 接口.获取QQ资料

```js
{
    "method": "getQQInfo",
    "params": {
        "qq": ""
    }
}
```

### 接口.获取群资料

```js
{
    "method": "getGroupInfo",
    "params": {
        "group": ""
    }
}
```

### 接口.邀请好友入群

```js
{
    "method": "inviteIntoGroup",
    "params": {
        "qq"    : "",
        "group" : ""
    }
}
```

无返回值

### 接口.设置群名片

```js
{
    "method": "setGroupCard", 
    "params": {
        "group" : "",
        "qq"    : "",
        "name"  : ""
    }
}
```

无返回值

### 接口.获取当前登录账号

```js
{
    "method": "getLoginAccount"
}
```

### 接口.设置个性签名

```js
{
    "method": "setSignature",
    "params": {
        "content": ""
    }
}
```

无返回值

### 接口.获取QQ昵称

```js
{
    "method": "getNickname",
    "params": {
        "qq": ""
    }
}
```

### 接口.获取名片点赞数量

```js
{
    "method": "getPraiseCount",
    "params": {
        "qq": ""
    }
}
```

### 接口.点赞名片

```js
{
    "method": "givePraise",
    "params": {
        "qq": ""
    }
}
```

无返回值

### 接口.处理好友请求

```js
{
    "method": "handleFriendRequest",
    "params": {
        "qq"       : "",
        "type"     : 1,      // 1=同意、2=拒绝、3=忽略
        "message"  : ""      // 拒绝理由，仅在拒绝请求时有效
    }
}
```

无返回值

### 接口.设置在线状态

```js
{
    "method": "setState",
    "params": {
        "type": 1  // 1=我在线上、2=Q我吧、3=离开、4=忙碌、5=请勿打扰、6=隐身
    }
}
```

无返回值

### 接口.处理加群请求

```js
{
    "method": "handleGroupRequest",
    "params": {
        "group"     : "",
        "qq"        : "",
        "seq"       : "",   // 加群请求事件提供的序列号
        "type"      : 1,    // 1=同意、2=拒绝、3=忽略
        "message"   : ""    // 拒绝时的拒绝理由，其它情况忽略
    }
}
```

无返回值

### 接口.移除群成员

```js
{
    "method": "kickGroupMember",
    "params": {
        "group" : "",
        "qq"    : ""
    }
}
```

无返回值

### 接口.禁言

```js
{
    "method": "silence",
    "params": {
        "group"     : "",
        "qq"        : "",
        "duration"  : 0     // 禁言时间，单位为秒，为0时解除禁言
    }
}
```

无返回值

### 接口.全体禁言

```js
{
    "method": "globalSilence",
    "params": {
        "group"   : "",
        "enable"  : true     // true为全体禁言,false为取消全体禁言
    }
}
```

无返回值

### 接口.获取Cookies

```js
{
    "method": "getCookies"
}
```

### 接口.获取Bkn

```js
{
    "method": "getBkn"
}
```

### 接口.获取ClientKey

```js
{
    "method": "getClientKey"
}
```

### 替换符.at

在发送的群消息中使用`[QQ:at=xxx]`表示at某个群成员，其中`xxx`可以替换为任意群成员QQ

使用`[QQ:at=all]`表示at全体成员，但仅在发送者为群管理员时有效

### 替换符.face/emoji

在发送的消息中使用`[QQ:face=212]`代表一个QQ表情，其中`212`为表情代码，使用`[QQ:emoji=39091]`代表一个emoji表情

这里不提供QQ表情和emoji表情的表情代码，你需要自己想办法获取它们

### 替换符.image/flash

发送消息中使用`[QQ:pic=xxx]`代表一张图片，`[QQ:flash,pic=xxx]`代表一张闪照

其中`xxx`可以是图片`GUID`，也可以是图片URL，图片`GUID`可以通过调用[接口.上传图片](#接口上传图片)接口获得

## 编译环境

MinGW 3.4.5

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
