# [libwsk](https://github.com/mirokaku/libwsk)

[![Actions Status](https://github.com/MiroKaku/libwsk/workflows/CodeQL/badge.svg)](https://github.com/MiroKaku/libwsk/actions)
[![LICENSE](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/MiroKaku/libwsk/blob/master/LICENSE)
![Windows](https://img.shields.io/badge/Windows-7+-orange.svg)
![Visual Studio](https://img.shields.io/badge/Visual%20Studio-2022-purple.svg)
[![Downloads](https://img.shields.io/nuget/dt/Musa.libwsk?logo=NuGet&logoColor=blue)](https://www.nuget.org/packages/Musa.libwsk/)

* [英文](README.md)

## 关于

libwsk 是对 WSK [(Winsock-Kernel)](https://docs.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel) 接口的封装。让内核模式驱动可以使用用户模式的 Winsock2 相同的套接字概念和接口进行网络 I/O 操作。

## 编译和使用

### Windows 10 or higher

右键单击该项目并选择“管理 NuGet 包”，然后搜索`Musa.libwsk`并选择适合你的版本，最后单击“安装”。

### Windows 7

1. 首先修改设置：

```
libwsk 属性页 -> Driver Settings -> Target OS Version = Windows 7
libwsk 属性页 -> Driver Settings -> Target Platform   = Desktop
```

2. 执行 BuildAllTargets.cmd


## 完成度

| BSD sockets   | WSA (Windows Sockets API)    | WSK (Windows Sockets Kernel) | State  
| ---           | ---                          | ---                          | :----: 
| -             | ~~WSAStartup~~               | WSKStartup                   |   √    
| -             | ~~WSACleanup~~               | WSKCleanup                   |   √    
| socket        | ~~WSASocket~~                | WSKSocket                    |   √    
| closesocket   | ~~WSASocket~~                | WSKCloseSocket               |   √    
| bind          | -                            | WSKBind                      |   √    
| listen        | -                            | WSKListen                    |   √    
| connect       | ~~WSAConnect~~               | WSKConnect                   |   √    
| shutdown      | ~~WSA[Recv/Send]Disconnect~~ | WSKDisconnect                |   √    
| accept        | ~~WSAAccept~~                | WSKAccept                    |   √    
| send          | ~~WSASend~~                  | WSKSend                      |   √    
| recv          | ~~WSARecv~~                  | WSKRecv                      |   √    
| sendto        | ~~WSASendTo~~                | WSKSendTo                    |   √    
| recvfrom      | ~~WSARecvFrom~~              | WSKRecvFrom                  |   √    
| ioctlsocket   | ~~WSAIoctl~~                 | WSKIoctl                     |   √    
| setsockopt    | -                            | WSKSetSocketOpt              |   √    
| getsockopt    | -                            | WSKGetSocketOpt              |   √    
| getaddrinfo   | ~~GetAddrInfoEx~~            | WSKGetAddrInfo               |   √    
| freeaddrinfo  | ~~FreeAddrInfoEx~~           | WSKFreeAddrInfo              |   √    
| getnameinfo   | ~~GetNameInfo~~              | WSKGetNameInfo               |   √    
| inet_ntoa     | ~~WSAAddressToString~~       | WSKAddressToString           |   √    
| inet_addr     | ~~WSAStringToAddress~~       | WSKStringToAddress           |   √    
| -             | ~~WSACreateEvent~~           | WSKCreateEvent               |   √    
| -             | ~~WSAGetOverlappedResult~~   | WSKGetOverlappedResult       |   √    
| ...           | ...                          | ...                          |   -    

## 引用参考

* [wbenny/KSOCKET](https://github.com/wbenny/KSOCKET)
* [microsoft/docs](https://docs.microsoft.com/zh-cn/windows-hardware/drivers/network/introduction-to-winsock-kernel)
