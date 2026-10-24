# 第三方依赖与分发边界

核对日期：2026-10-24。本文对应当前模块结构，版本下限见 [README](README.md)。实际交付须核对所用版本、构建选项、随包文件和取得的授权；本文不替代上游许可，也不授予第三方软件的再分发权。

## HandleCore 的 MIT 范围

本仓库自有源码、构建脚本、测试和文档采用 [MIT](LICENSE)，版权署名为周涛（ZHOU TAO）。外部依赖仍属于各自权利人，不因链接 HandleCore 或与其一同打包而改为 MIT。第三方原有声明不得以本仓库署名替换。

当前 Git 仓库不随附 Qt、libserialport、libpcap/Npcap 的源码、SDK、运行库或驱动；CMake 引用使用环境提供的依赖。仅发布本仓库源码，与分发含依赖的可执行程序、安装包、容器镜像或设备固件，是不同的分发场景。本文是依赖说明，不是后者所需完整许可证和版权声明的替代品。

## 模块与许可证

| 依赖 | 使用范围 | 许可证与官方依据 |
|---|---|---|
| Qt Core | Core 及全部可选模块 | 提供 LGPLv3 和商业许可等选择；本文说明 LGPLv3 路线。[Qt Core 许可与归属](https://doc.qt.io/qt-6.8/qtcore-index.html#licenses-and-attributions)、[LGPLv3 全文](https://doc.qt.io/qt-6/lgpl.html) |
| libserialport | 仅启用 `HANDLECORE_WITH_SERIAL` 时 | LGPL-3.0-or-later。[官方仓库](https://github.com/sigrokproject/libserialport)、[COPYING](https://github.com/sigrokproject/libserialport/blob/master/COPYING) |
| libpcap | Linux 上的 Pcap 模块 | BSD 三条款；保留所用版本的版权、条件与免责声明。[官方 LICENSE](https://github.com/the-tcpdump-group/libpcap/blob/master/LICENSE) |
| Npcap SDK / 运行库 / 驱动 | Windows 上的 Pcap 模块 | Npcap 运行库/驱动采用独立许可，不是普通开源许可；SDK 及其文件按各自所附条款处理。[官方许可](https://npcap.com/license)、[SDK 开发说明](https://npcap.com/guide/npcap-devguide.html) |

SimGimbal、SimCard 没有额外串口或 Pcap 依赖。所有可选模块默认关闭；仅启用仿真或串口不会引入 Npcap。Qt 和抓包发行包内还可能含其他第三方组件，须按实际包的归属清单保留对应声明，不能仅附本表。

## Qt Core 与 libserialport：LGPL 路线

使用 LGPL 库并不要求 HandleCore 或独立应用的自有代码一律采用 LGPL；符合 LGPL 条件时，可以用于闭源或商业应用。Qt 的商业授权不覆盖 libserialport；选择 Qt 商业路线时还须遵守自己的授权合同及其他依赖的条款。

Windows 的 DLL 和 Linux 的共享库 `.so` 都适合当前接入方式。动态链接可以简化重新链接义务，但不是全部合规工作的替代品。对外分发使用 LGPL 库的程序时，应按实际场景落实：

- 明确告知使用哪些 LGPL 库，随交付提供 GPLv3、LGPLv3 许可文本以及相关版权声明。
- 随包分发库二进制时，按适用条款提供对应版本的完整源码，包括修改与必要构建材料，或采用条款允许的源码提供方式；仅放上游首页链接不足以承担分发者的源码提供责任。
- 允许用户使用接口兼容的修改版库，不得禁止为调试库修改所需的逆向工程；对库本身的修改按 LGPL 等适用条款交付。
- 静态链接并非一律禁止，但通常需要提供可供重新链接的应用目标文件、脚本等材料。适用 LGPLv3 安装信息条款的设备，还须提供安装和运行修改版本所需的信息。

依据：[Qt 官方 LGPL 使用义务](https://www.qt.io/faq/qt-open-source-licensing)、[LGPLv3 第 3、4 条](https://doc.qt.io/qt-6/lgpl.html)。这只覆盖 HandleCore 使用的 Qt Core；上层应用增加其他 Qt 模块时需另查其许可。

用户自行通过发行版安装依赖，可以减少我们随包分发的第三方文件；若交付包含这些库的 Docker 镜像或 ARM 系统镜像，仍需承担相应分发义务。无窗口、跨架构或使用包管理器，不会改变实际交付内容的许可证。

## Linux libpcap

BSD 三条款允许源码和二进制的使用、修改及再分发，不要求公开上层业务源码。源码分发保留版权、许可条件和免责声明；二进制分发在文档或其他随附材料中保留它们；不得未经许可使用作者名称为产品背书。具体声明取自实际使用版本，不能用 HandleCore 的 MIT 文件替代。[官方条款](https://github.com/the-tcpdump-group/libpcap/blob/master/LICENSE)

## Windows Npcap

Npcap 的源码可查阅，不表示其整体采用开源许可证，也不能因为内部包含 BSD 许可的 libpcap 就按 BSD 分发整个 Npcap。

- 免费许可通常有五份、五台及五用户限制，并有针对指定软件的例外；HandleCore 和 LiDAR 应用不在这些指定软件例外中。自行下载安装并不免除安装数量等限制。
- 使用 SDK 开发兼容程序不等于获得捆绑再分发 Npcap 的权利。本仓库不分发 SDK、`wpcap.dll`、`Packet.dll`、驱动或安装器，也不把其代码重新标为 MIT。
- 未取得相应授权时，让接收方从官方自行下载安装 Npcap；这是官方向开源软件作者建议的方式。不要将构建目录中来自 Npcap 的文件直接收进交付包。
- 随产品分发需要相应再分发授权；组织内部超出免费范围需要相应内部使用授权。两类 OEM 授权用途不同，不能互相代替，实际权利以取得的协议为准。

依据：[Npcap 许可全文](https://npcap.com/license)、[OEM 再分发](https://npcap.com/oem/redist.html)、[OEM 内部使用](https://npcap.com/oem/internal.html)。以上限制针对 Npcap；Linux 直接使用 libpcap 时按前节处理。

## 发布材料归属

HandleCore 源码发布保留根目录 LICENSE，并附本依赖说明。上层产品的发布者根据实际启用模块和随包文件，准备第三方许可文本、版权声明、需要提供的源码/构建材料及必要授权。README 的链接用于定位依据，不能自动代替随二进制交付所要求的材料。
