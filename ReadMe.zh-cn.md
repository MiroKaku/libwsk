# [libwsk](https://github.com/mirokaku/libwsk)

[![Actions Status](https://github.com/MiroKaku/libwsk/workflows/CodeQL/badge.svg)](https://github.com/MiroKaku/libwsk/actions)
[![LICENSE](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/MiroKaku/libwsk/blob/master/LICENSE)
![Windows](https://img.shields.io/badge/Windows-7+-orange.svg)
![Visual Studio](https://img.shields.io/badge/Visual%20Studio-2019-purple.svg)

## 关于

libwsk 是对 WSK [(Winsock-Kernel)](https://docs.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel) 接口的封装。让内核模式驱动可以使用用户模式的 Winsock2 相同的套接字概念和接口进行网络 I/O 操作。

## 编译和使用

IDE：Visual Studio 2019 or higher

> 如果目标系统是 Windows7，请设置这些。
> ```
> libwsk 属性页 -> Driver Settings -> Target OS Version = Windows 7
> libwsk 属性页 -> Driver Settings -> Target Platform   = Desktop
> ```

1. `git clone --recurse-submodules https://github.com/MiroKaku/libwsk.git`
2. 打开 `msvc/libwsk.sln` 并编译它。
3. 引入 `libwsk.lib` 到你的项目。参考 `unittest`。

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
