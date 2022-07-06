# [libwsk](https://github.com/mirokaku/libwsk)

[![Actions Status](https://github.com/MiroKaku/libwsk/workflows/CodeQL/badge.svg)](https://github.com/MiroKaku/libwsk/actions)
[![LICENSE](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/MiroKaku/libwsk/blob/master/LICENSE)
![Windows](https://img.shields.io/badge/Windows-7+-orange.svg)
![Visual Studio](https://img.shields.io/badge/Visual%20Studio-2019-purple.svg)

* [简体中文](ReadMe.zh-cn.md)

## About

libwsk is a wrapper for the [WSK (Winsock-Kernel)](https://docs.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel) interface. With libwsk, kernel-mode software modules can perform network I/O operations using the same socket programming concepts and interface that are supported by user-mode Winsock2.

## Build and used

IDE：Visual Studio 2019 or higher

> if target OS is Windows7, please set these.
> ```
> libwsk property pages -> Driver Settings -> Target OS Version = Windows 7
> libwsk Property pages -> Driver Settings -> Target Platform   = Desktop
> ```

1. `git clone --recurse-submodules https://github.com/MiroKaku/libwsk.git`
2. Open the `msvc/libwsk.sln` and build it.
3. Include `libwsk.lib` to your project. refer `unittest`.

## Supported progress

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

## Reference

* [wbenny/KSOCKET](https://github.com/wbenny/KSOCKET)
* [microsoft/docs](https://docs.microsoft.com/zh-cn/windows-hardware/drivers/network/introduction-to-winsock-kernel)
