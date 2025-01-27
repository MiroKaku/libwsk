# [libwsk](https://github.com/mirokaku/libwsk)

[![Actions Status](https://github.com/MiroKaku/libwsk/workflows/CodeQL/badge.svg)](https://github.com/MiroKaku/libwsk/actions)
[![LICENSE](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/MiroKaku/libwsk/blob/master/LICENSE)
![Windows](https://img.shields.io/badge/Windows-7+-orange.svg)
![Visual Studio](https://img.shields.io/badge/Visual%20Studio-2022-purple.svg)
[![Downloads](https://img.shields.io/nuget/dt/Musa.libwsk?logo=NuGet&logoColor=blue)](https://www.nuget.org/packages/Musa.libwsk/)

* [简体中文](README.zh-CN.md)

## About

libwsk is a wrapper for the [WSK (Winsock-Kernel)](https://docs.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel) interface. With libwsk, kernel-mode software modules can perform network I/O operations using the same socket programming concepts and interface that are supported by user-mode Winsock2.

## Build and used

### Windows 10 or higher

Right click on the project, select "Manage NuGet Packages".
Search for `Musa.libwsk`, choose the version that suits you, and then click "Install".

### Windows 7

1. First modify the settings:

```
libwsk property pages -> Driver Settings -> Target OS Version = Windows 7
libwsk Property pages -> Driver Settings -> Target Platform   = Desktop
```

2. Call BuildAllTargets.cmd

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
