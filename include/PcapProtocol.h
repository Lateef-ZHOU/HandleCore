#pragma once
#include <algorithm>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>
#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <QtEndian>
#include <QtGlobal>

// Ethernet / ARP / IPv4 / UDP 值对象与固定格式 BPF 谓词。
// 当前只处理普通以太帧、20 字节 IPv4 头及非分片 UDP；VLAN 等格式另行扩展。

// ------------------------------------------------------------
// MAC — 链路层地址值对象 = 定长字节序列本身（底座 QByteArray）。恒 6 字节是构造纪律而非
// 类型性质：构造口全部定长自证（checked 兜底 Q_ASSERT），Size 为定长唯一声明点。
// 全零 = 未配置占位（线上非法值兼作哨兵）。填帧 = 序列拷贝、取字段 = mid 一行。
// ------------------------------------------------------------
class MAC : public QByteArray
{
public:
    static constexpr int Size = 6; // 定长唯一声明点（ARP 的 HLEN 即此值）

    MAC() : QByteArray(Size, '\0') {} // 全零 = 未配置占位（成员/容器缺省态可判 isNull）

    // 六字节序列收编（mid / fromHex / WinAPI 缓冲等一切来源）——定长由调用现场自证，checked 兜底。
    explicit MAC(QByteArray bytes) : QByteArray(checked(std::move(bytes))) {}

    [[nodiscard]] static MAC broadcast() { return MAC(QByteArray(Size, '\xFF')); }

    // 书写形解析（"AA-BB-CC-DD-EE-FF" / "AA:BB:CC:DD:EE:FF"）：UI/配置侧唯一入口。只此一个
    // 签名——不设忽略 ok 的便捷版：解析失败是调用方的问题，值对象不替它掩盖。
    [[nodiscard]] static MAC fromString(const QString &str, bool &ok)
    {
        QString cleaned = str;
        cleaned.remove(QLatin1Char(':'));
        cleaned.remove(QLatin1Char('-'));
        static const QRegularExpression hexRe(QStringLiteral("^[0-9A-Fa-f]{12}$"));
        ok = hexRe.match(cleaned).hasMatch();
        if (!ok)
            return {};
        return MAC(QByteArray::fromHex(cleaned.toLatin1()));
    }

    [[nodiscard]] QString toString() const { return QString::fromLatin1(toHex('-').toUpper()); }

    [[nodiscard]] bool isNull() const
    {
        static const QByteArray kNull(Size, '\0'); // 零值静态常量化，不逐调用现造
        return *this == kNull;
    }

private:
    // 定长纪律的兜底：六字节不足/超出即调用方编程错误。
    [[nodiscard]] static QByteArray checked(QByteArray bytes)
    {
        Q_ASSERT(bytes.size() == Size);
        return bytes;
    }
};

// ------------------------------------------------------------
// IPv4 — 网络层地址值对象 = 定长字节序列本身（同 MAC 判例）：点分十进制就是字节序列的逐
// 字节十进制书写，线上形与值形合一——存储即网络序，全文件 IP 零字节序转换。
// 恒 4 字节 = 构造纪律。全零 = 0.0.0.0 未设占位。
// ------------------------------------------------------------
class IPv4 : public QByteArray
{
public:
    static constexpr int Size = 4; // 定长唯一声明点（ARP 的 PLEN 即此值）

    IPv4() : QByteArray(Size, '\0') {} // 0.0.0.0 = 未设

    // 四字节序列收编——定长由调用现场自证，checked 兜底。
    explicit IPv4(QByteArray bytes) : QByteArray(checked(std::move(bytes))) {}

    // 点分十进制解析（UI/配置侧唯一入口；只此一个签名，同 MAC）。段宽可变（"1.2.3.4" /
    // "192.168.001.100"）不对齐，无一次性转换可借——四段直写四遍（四是协议常数不是集合）。
    [[nodiscard]] static IPv4 fromString(const QString &str, bool &ok)
    {
        ok = false;
        const QStringList parts = str.split(QLatin1Char('.'));
        if (parts.size() != Size) // 段数即字节数——从定长声明点拿，不另立字面量
            return {};
        bool ok0 = false, ok1 = false, ok2 = false, ok3 = false;
        const uint s0 = parts[0].toUInt(&ok0);
        const uint s1 = parts[1].toUInt(&ok1);
        const uint s2 = parts[2].toUInt(&ok2);
        const uint s3 = parts[3].toUInt(&ok3);
        if (!ok0 || !ok1 || !ok2 || !ok3 || s0 > 255 || s1 > 255 || s2 > 255 || s3 > 255)
            return {};
        ok = true;
        const char bytes[] = {char(s0), char(s1), char(s2), char(s3)};
        return IPv4(QByteArray(bytes, Size));
    }

    [[nodiscard]] QString toString() const
    {
        const auto *b = reinterpret_cast<const unsigned char *>(constData());
        return QString::asprintf("%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }

private:
    // 定长纪律的兜底：四字节不足/超出即调用方编程错误。
    [[nodiscard]] static QByteArray checked(QByteArray bytes)
    {
        Q_ASSERT(bytes.size() == Size);
        return bytes;
    }
};

// ------------------------------------------------------------
// Filter — BPF 过滤器值对象（内核预滤谓词）。继承 QString：过滤器"是"pcap_compile 的表达
// 式串——串是机器交付物、人不看，正确性由构造机制担保。
// 谓词 = 帧种的第三面：偏移取各层 kXxxOffset（TX 填 / RX 读 / 谓词滤共用一张表，抄错三面
// 同炸即交叉验证）、字宽 = 重载签名本身、值 = 枚举常量 / 值对象字节。tcpdump 方言整体
// 不用——字节谓词 ether[off:size] = val 才是机制层，手写方言 = 无约束的第四份平行知识。
// 组合子仅 &（and）/ |（or），构造即括号：BPF 方言 and 优先于 or，裸串拼接漏括号 = 静默
// 错滤（嵌套冗余括号无害，人不看）。带参构造私有：合法子句只能出自 match 原语与组合运算；
// 公开默认构造 = 恒真谓词（空串 = 无约束——& 的单位元、| 的吸收元），是 any 字段"省略
// 子句"的机制底座。
// 摊平范式：每层谓词参数 = 本层 ctor 字段集去缓冲/载荷；章参数（Type/Op/Proto）由收口者
// 报入且前置必填（默认实参语法不许有默认者后跟无默认者），业务字段全员 any-able
// （std::optional 带空默认，nullopt = 不约束省略子句）；中间层 protected、叶层 public——
// 与 ctor 链域格局对齐。"哪些字段关心"的业务知识不住帧类，收拢到调用点（buildFilter）。
// "过滤器筛字段、裁决认种类"分工：滤器是字段级筛选的唯一岗（放行的都是要的，甄别器不
// 重复判字段）；甄别只做种类分派（default drop = 完备性兜底，非字段防线）。
// 假定注记：字节谓词写死偏移即 IHL=20（无选项 IP 头）——与 RX 侧偏移表同一假定。
// ------------------------------------------------------------
class Filter : public QString
{
public:
    Filter() = default; // 恒真谓词（无约束）：& 的单位元 / | 的吸收元——any 字段的机制底座

    // ── 字节谓词原语：帧内 [offset, offset + 宽) 的大端值 == value → "ether[off:size] = 0x…"。
    // BPF 的 1 / 2 字节宽即两个数值重载的签名本身，字宽分派交给重载决议——枚举调用点
    // qToUnderlying 落到底型宽的重载；值对象经 QByteArray 基类引用落到序列版（贪心拆段，4 字节段在那儿自己拼）。
    // 三宽是 cBPF 指令集的硬边界（32 位累加器机，加载仅 BPF_B/H/W——[off:size] 超 4 是
    // pcap_compile 语法错），非风格选择：方言的 ether host 编译后同样拆段，方言只是把拆段
    // 藏进编译器。
    [[nodiscard]] static Filter match(const int offset, const quint8 value)
    {
        return Filter(QStringLiteral("ether[%1:1] = 0x%2").arg(offset).arg(value, 0, 16));
    }

    [[nodiscard]] static Filter match(const int offset, const quint16 value)
    {
        return Filter(QStringLiteral("ether[%1:2] = 0x%2").arg(offset).arg(value, 0, 16));
    }

    [[nodiscard]] static Filter match(const int offset, const QByteArray &value) // 序列版：4/2/1 贪心拆段
    {
        Filter f;
        for (int pos = 0; pos < value.size();)
        {
            const int w = value.size() - pos >= 4 ? 4 : value.size() - pos >= 2 ? 2 : 1;
            f = f & Filter(QStringLiteral("ether[%1:%2] = 0x%3")
                               .arg(offset + pos)
                               .arg(w)
                               .arg(QString::fromLatin1(value.mid(pos, w).toHex())));
            pos += w;
        }
        return f;
    }

    // ── any 皮：any-able 字段以 std::optional 表达"未申告"。nullopt = 不约束，贡献恒真、
    // 子句省略；有值即解包转发——分派仍归四裸重载的重载决议（四裸重载自己不直接收 optional：
    // 裸整型对三个 optional 数值版三路可转 = 重载歧义）。
    template <typename V>
    [[nodiscard]] static Filter match(const int offset, const std::optional<V> &value)
    {
        return value ? match(offset, *value) : Filter{};
    }

    // 组合子（构造即括号；恒真按代数律短路：真∧x=x、真∨x=真）。
    [[nodiscard]] Filter operator&(const Filter &rhs) const
    {
        if (isEmpty())
            return rhs;
        if (rhs.isEmpty())
            return *this;
        return Filter(QStringLiteral("(%1) and (%2)").arg(*this, rhs));
    }

    [[nodiscard]] Filter operator|(const Filter &rhs) const
    {
        if (isEmpty() || rhs.isEmpty())
            return {};
        return Filter(QStringLiteral("(%1) or (%2)").arg(*this, rhs));
    }

private:
    explicit Filter(QString clause) : QString(std::move(clause)) {}
};

// ------------------------------------------------------------
// EtherFrame — 链路层基座。单居民写死：Npcap managed 模式下以太/Wi-Fi 统一伪以太帧，层内
// 多态等居民进场再立，抽象不提前。帧"是"字节序列（继承 QByteArray）：字段不另存变量、
// 信息值从字节现取，构造即有完整报文——final 层构造前算定总长现场分配，私有静态 fill 链
// 自尾向前逐段填充（传入引用→填本层段→传出引用），至本基座 move 收编，构造体全空。
// 帧对象可直接当 QByteArray 用（返回即隐式收窄，CoW 浅拷零字节复制）。
// 构造 protected（成帧的必是某个 final 类），种类由分支自盖；RX：typeOf 读章供甄别，
// protected explicit(QByteArray) 过境收编——公开收编口只开在甄别口担保的叶层。
// ------------------------------------------------------------
class EtherFrame : public QByteArray
{
public:
    // 底型 = 线上字段宽的唯一声明点：写入按底型、尺寸问 sizeof——不设第二个手写 quint16
    // 与之"碰巧一致"。
    enum class Type : quint16
    {
        IPv4 = 0x0800,
        ARP  = 0x0806,
    };

    // 头长 = 两地址 + 类型字（14B）——从字段的定长声明点导出，不立字面量。
    static constexpr int HeaderSize = 2 * MAC::Size + sizeof(Type);

    // RX 读章：线上分用键现取（甄别口 kindOf 专供，前提：已保长度 ≥ HeaderSize）。不在名单
    // 内的线上值照样成值（底型齐宽），switch default 兜住。
    [[nodiscard]] static Type typeOf(const QByteArray &frame)
    {
        return static_cast<Type>(qFromBigEndian<std::underlying_type_t<Type>>(frame.constData() + kTypeOffset));
    }

protected:
    // 构造体空——缓冲自叶层经实参链传抵，fillLink 填毕信封段后 move 进基类。
    EtherFrame(QByteArray &&frame, const MAC &dst, const MAC &src, const Type type)
        : QByteArray(fillLink(std::move(frame), dst, src, type))
    {
    }

    // RX 过境收编：完整线上报文原样入座（kindOf 已保结构长度与种类；视图 / CoW 皆浅共享
    // 零拷贝）。中间层不公开。
    explicit EtherFrame(QByteArray frame) : QByteArray(std::move(frame)) {}

    // 内核预滤谓词（谓词链起点）：type 章由上层分支报入；偏移/字宽与 fillLink / typeOf
    // 同源（填、读、滤共用一份机制事实）。
    [[nodiscard]] static Filter matchEther(const Type type, const std::optional<MAC> &dst = {},
                                           const std::optional<MAC> &src = {})
    {
        return Filter::match(kTypeOffset, qToUnderlying(type)) & Filter::match(kDstMACOffset, dst) &
               Filter::match(kSrcMACOffset, src);
    }

private:
    // 信封三字段偏移（字宽导出、链式推进——与 fillLink 顺序走笔同源；谓词与读章共用）。
    static constexpr int kDstMACOffset = 0;
    static constexpr int kSrcMACOffset = kDstMACOffset + MAC::Size;
    static constexpr int kTypeOffset   = kSrcMACOffset + MAC::Size; // 分用键居两地址之后

    // fill 链收口一环：填 [0, HeaderSize) 信封段——传入引用、填本层、传出引用。
    [[nodiscard]] static QByteArray &&fillLink(QByteArray &&frame, const MAC &dst, const MAC &src, const Type type)
    {
        auto *p = reinterpret_cast<unsigned char *>(frame.data());
        p = std::ranges::copy(dst, p).out; // 地址即字节序列——写出 = 序列拷贝
        p = std::ranges::copy(src, p).out;
        qToBigEndian(static_cast<std::underlying_type_t<Type>>(type), p); // 种类按底型写入
        return std::move(frame);
    }
};

// ------------------------------------------------------------
// ARPPacket — ARP 报文 = 链路层的一个继承分支（非 final：支内 ARPRequest / ARPReply 两
// final 收口）。定长缓冲现场分配，fillBody 填本体段后交基类 fill 链收口。构造 protected，
// Op 由两 final 自盖。读口 sender/target：字段从字节现取——RX 定长且四元组全用，整帧
// 材料化（收编口开在两 final 叶层，本类过境）。
// ------------------------------------------------------------
class ARPPacket : public EtherFrame
{
public:
    enum class Op : quint16 // 底型 = OPER 字段宽（2B）——写入按底型
    {
        Request = 0x0001,
        Reply   = 0x0002,
    };

    static constexpr int BodySize = 28; // HTYPE/PTYPE/HLEN/PLEN 固定段 + OPER + 两组 MAC/IP

    // RX 读章：OPER 现取（kindOf 专供，前提：长度 ≥ HeaderSize + BodySize）。op 是读出值
    // 不是类型，材料化仍到 ARPPacket 为止。
    [[nodiscard]] static Op opOf(const QByteArray &frame)
    {
        return static_cast<Op>(qFromBigEndian<std::underlying_type_t<Op>>(frame.constData() + kOpOffset));
    }

    // 读口：字段从字节现取（mid 一行——QByteArray 同底座的红利）。
    [[nodiscard]] MAC senderMAC() const { return MAC(mid(kSenderOffset, MAC::Size)); }

    [[nodiscard]] IPv4 senderIP() const { return IPv4(mid(kSenderOffset + MAC::Size, IPv4::Size)); }

    [[nodiscard]] IPv4 targetIP() const { return IPv4(mid(kTargetOffset + MAC::Size, IPv4::Size)); }

    // 内核预滤谓词：Type::ARP 章自盖；op 章亦 any-able（空 = 不判种类，一条谓词双语义——
    // targetIP 同字段天然覆盖"问本机的 Request"与"答本机的 Reply"，省略子句即代 pcap 做的
    // 合并优化，比 op=1 or op=2 双支更省）。四元组偏移与读口同一批表达式——填、读、滤一张表。
    [[nodiscard]] static Filter matchARP(const std::optional<Op> &op = {},
                                         const std::optional<MAC> &etherDst = {},
                                         const std::optional<MAC> &etherSrc = {},
                                         const std::optional<MAC> &senderMAC = {},
                                         const std::optional<IPv4> &senderIP = {},
                                         const std::optional<MAC> &targetMAC = {},
                                         const std::optional<IPv4> &targetIP = {})
    {
        return matchEther(Type::ARP, etherDst, etherSrc) &
               Filter::match(kOpOffset, op ? std::optional<quint16>(qToUnderlying(*op)) : std::nullopt) &
               Filter::match(kSenderOffset, senderMAC) &
               Filter::match(kSenderOffset + MAC::Size, senderIP) &
               Filter::match(kTargetOffset, targetMAC) &
               Filter::match(kTargetOffset + MAC::Size, targetIP);
    }

protected:
    // 构造体空：定长缓冲现场分配（总长构造前即知），fillBody 填毕本体段后沿实参链交基类
    // 收口；Type::ARP 自盖，Op 由 Request/Reply 收口者报入。
    ARPPacket(const MAC &etherDst, const MAC &etherSrc, const Op op, const MAC &senderMAC, const IPv4 senderIP,
              const MAC &targetMAC, const IPv4 targetIP)
        : EtherFrame(fillBody(QByteArray(HeaderSize + BodySize, Qt::Uninitialized), op, senderMAC, senderIP,
                              targetMAC, targetIP),
                     etherDst, etherSrc, Type::ARP)
    {
    }

    // RX 过境收编（中间层不公开；类型担保 = kindOf 的 case 标签）。
    explicit ARPPacket(QByteArray frame) : EtherFrame(std::move(frame)) {}

private:
    // OPER 居链路头 + HTYPE/PTYPE + HLEN/PLEN 之后，sender / target 二元组依次紧随
    // （偏移全由字宽导出、逐项链式推进，不立字面量）。
    static constexpr int kOpOffset     = HeaderSize + 2 * sizeof(quint16) + 2 * sizeof(quint8);
    static constexpr int kSenderOffset = kOpOffset + sizeof(Op);
    static constexpr int kTargetOffset = kSenderOffset + MAC::Size + IPv4::Size;

    // 填本体段 [HeaderSize, +BodySize)：固定段烤死 + OPER + 四元组——传入引用、填毕传出。
    [[nodiscard]] static QByteArray &&fillBody(QByteArray &&frame, const Op op, const MAC &senderMAC,
                                               const IPv4 senderIP, const MAC &targetMAC, const IPv4 targetIP)
    {
        auto *p = reinterpret_cast<unsigned char *>(frame.data()) + HeaderSize;
        qToBigEndian<quint16>(0x0001, p); // HTYPE = 以太（非枚举的线上常数，字段宽 2B 直书）
        p += sizeof(quint16);
        qToBigEndian(static_cast<std::underlying_type_t<Type>>(Type::IPv4), p); // PTYPE：所解析的协议是 IPv4（词汇同 EtherType，按底型写入）
        p += sizeof(Type);
        *p++ = MAC::Size;  // HLEN
        *p++ = IPv4::Size; // PLEN
        qToBigEndian(static_cast<std::underlying_type_t<Op>>(op), p); // OPER 按底型写入
        p += sizeof(op);
        p = std::ranges::copy(senderMAC, p).out; // 地址即字节序列——写出 = 序列拷贝
        p = std::ranges::copy(senderIP, p).out;
        p = std::ranges::copy(targetMAC, p).out;
        std::ranges::copy(targetIP, p);
        return std::move(frame);
    }
};

// ------------------------------------------------------------
// ARPRequest — ARP 探测请求（final）：以太广播问全网"targetIP 的 MAC 是谁"（target MAC
// 置零 = 所问即未知）；Op::Request 自盖。
// ------------------------------------------------------------
class ARPRequest final : public ARPPacket
{
public:
    ARPRequest(const MAC &senderMAC, const IPv4 senderIP, const IPv4 targetIP)
        : ARPPacket(MAC::broadcast(), senderMAC, Op::Request, senderMAC, senderIP, MAC{}, targetIP)
    {
    }

    // RX 收编口：kindOf 甄别为 ARPRequest 后整帧入座（case 标签即类型担保），四元组经读口现取。
    explicit ARPRequest(QByteArray frame) : ARPPacket(std::move(frame)) {}
};

// ------------------------------------------------------------
// ARPReply — ARP 应答（final）：以太单播回问方；Op::Reply 自盖。
//   · 普通构造：自报 sender 二元组 + 指名 target 二元组（以太目的 = target MAC）；
//   · 便捷构造：收一份请求 + 本机 MAC——应答是请求的镜像（target 二元组照抄请求的 sender
//     二元组），且本机 IP 就是请求所问的 targetIP（问到谁、谁作答），一并从请求字节现取。
// ------------------------------------------------------------
class ARPReply final : public ARPPacket
{
public:
    ARPReply(const MAC &senderMAC, const IPv4 senderIP, const MAC &targetMAC, const IPv4 targetIP)
        : ARPPacket(targetMAC, senderMAC, Op::Reply, senderMAC, senderIP, targetMAC, targetIP)
    {
    }

    // 便捷构造：谁问答谁（委托普通构造——本机 IP = request.targetIP()，target = 请求的 sender）。
    ARPReply(const ARPRequest &request, const MAC &selfMAC)
        : ARPReply(selfMAC, request.targetIP(), request.senderMAC(), request.senderIP())
    {
    }

    // RX 收编口：kindOf 甄别为 ARPReply 后整帧入座（学习项 = sender 二元组，经读口现取）。
    explicit ARPReply(QByteArray frame) : ARPPacket(std::move(frame)) {}
};

// ------------------------------------------------------------
// IPv4Packet — IPv4 报文 = 链路层的另一个继承分支（非 final：传输层在其上收口，构造
// protected）。fillNetwork 填本层头段，totalLen / 校验和就地现算现盖（totalLen = 整帧 −
// 链路头：缓冲总长自明）。分用键声明住本层（Proto 枚举），值由传输分支构造期自盖。
// 本层字段（TTL / 校验和等）无 RX 消费者不设读口（读口按消费价值定）。
// ------------------------------------------------------------
class IPv4Packet : public EtherFrame
{
public:
    enum class Proto : quint8 // 底型 = IP 头 protocol 字段宽（1B）——写入按底型
    {
        UDP = 0x11, // 枚举只命名本工程认识的种类（TCP/ICMP 等居民进场再添）
    };

    static constexpr int HeaderSize = 20; // 无选项 IHL=5 恒 20B（有意遮蔽基类同名——各层报自己的头长）

    // RX 读章：IP 头 protocol 字段现取（kindOf 专供，前提：长度已保覆盖本层头）。
    [[nodiscard]] static Proto protoOf(const QByteArray &frame)
    {
        return static_cast<Proto>(qFromBigEndian<std::underlying_type_t<Proto>>(frame.constData() + kProtoOffset));
    }

protected:
    // 构造体空：缓冲实参链过境，fillNetwork 填毕本层头段后传下；Type::IPv4 自盖。
    IPv4Packet(QByteArray &&frame, const MAC &etherDst, const MAC &etherSrc, const IPv4 srcIP,
               const IPv4 dstIP, const Proto proto)
        : EtherFrame(fillNetwork(std::move(frame), srcIP, dstIP, proto), etherDst, etherSrc, Type::IPv4)
    {
    }

    // RX 过境收编（中间层不公开；叶层收编口经此过境）。
    explicit IPv4Packet(QByteArray frame) : EtherFrame(std::move(frame)) {}

    // 内核预滤谓词：Type::IPv4 章自盖，proto 章由传输层收口者报入；中间层谓词不公开，
    // 对齐 ctor 链域格局。
    [[nodiscard]] static Filter matchIPv4(const Proto proto, const std::optional<MAC> &etherDst = {},
                                          const std::optional<MAC> &etherSrc = {},
                                          const std::optional<IPv4> &srcIP = {},
                                          const std::optional<IPv4> &dstIP = {})
    {
        return matchEther(Type::IPv4, etherDst, etherSrc) & Filter::match(kProtoOffset, qToUnderlying(proto)) &
               Filter::match(kSrcIPOffset, srcIP) & Filter::match(kDstIPOffset, dstIP);
    }

private:
    // protocol 居版本/IHL、TOS、TTL 三个单字节与 totalLen/ID/flags 三个双字节之后（RFC 791
    // 固定偏移——选项在头 20B 之后，本偏移不受 IHL 影响）；src / dst 地址居 protocol +
    // 校验和之后（链式推进，与 fill 顺序走笔同源）。
    static constexpr int kProtoOffset = EtherFrame::HeaderSize + 3 * sizeof(quint8) + 3 * sizeof(quint16);
    static constexpr int kSrcIPOffset = kProtoOffset + sizeof(Proto) + sizeof(quint16);
    static constexpr int kDstIPOffset = kSrcIPOffset + IPv4::Size;

    // 填本层头段：固定值烤死 + totalLen/校验和现算现盖——传入引用、填毕传出。
    [[nodiscard]] static QByteArray &&fillNetwork(QByteArray &&frame, const IPv4 srcIP, const IPv4 dstIP,
                                                  const Proto proto)
    {
        auto *p = reinterpret_cast<unsigned char *>(frame.data()) + EtherFrame::HeaderSize;
        const auto *const head = p;
        *p++ = 0x45; // 版本/IHL
        *p++ = 0x00; // TOS
        qToBigEndian(static_cast<quint16>(frame.size() - EtherFrame::HeaderSize), p); // totalLen = 整帧 − 链路头
        p += sizeof(quint16);
        qToBigEndian<quint16>(0x0001, p); // ID
        p += sizeof(quint16);
        qToBigEndian<quint16>(0x4000, p); // DF
        p += sizeof(quint16);
        *p++ = 0x40; // TTL = 64
        qToBigEndian(static_cast<std::underlying_type_t<Proto>>(proto), p); // protocol 按底型写入（1B 大端即本身）
        p += sizeof(proto);
        auto *const slot = p; // 校验和占位（参与求和时须为零——RFC 791 规程）
        *p++ = 0;
        *p++ = 0;
        p = std::ranges::copy(srcIP, p).out; // 地址即字节序列——写出 = 序列拷贝
        p = std::ranges::copy(dstIP, p).out;
        qToBigEndian(checksum(head), slot); // 字段全落位后盖章
        return std::move(frame);
    }

    // RFC 791 首部校验和：16 位反码和（求和时校验和字段为零）。
    [[nodiscard]] static quint16 checksum(const unsigned char *header)
    {
        quint32 sum = 0;
        for (int i = 0; i < HeaderSize; i += 2)
            sum += static_cast<quint16>(header[i]) << 8 | header[i + 1];
        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<quint16>(~sum);
    }
};

// ------------------------------------------------------------
// UDPDatagram — UDP 数据报 = IPv4 分支上的传输层收口（final；各层类名沿用各自 PDU 本名：
// 帧 / 包 / 数据报）。总长在此算定并现场分配（三层头 + 载荷），fillTrans 填本层段 + 载荷
// 后沿实参链下传，RFC 768 校验和就地现算（伪首部取实参，载荷先落位再求和）。Proto::UDP
// 自盖。UDP 是通用运输，构造公开。
// RX：收编口 + 载荷双读口 body / bodyView——裁切按 UDP 头长度字段、不按帧长切（以太帧短
// 于 60B 物理层垫底，按帧长切会把垫底字节当载荷——大包采集卡踩不到，小载荷设备必踩）；
// 头部四元组 BPF 已验明正身、无消费价值不设读口。
// ------------------------------------------------------------
class UDPDatagram final : public IPv4Packet
{
public:
    // 头长 = srcPort / dstPort / len / checksum 四字（8B）——从字段类型导出，不立字面量
    // （遮蔽基类同名——各层报自己的头长）。
    static constexpr int HeaderSize = 4 * sizeof(quint16);

    // 构造体空：缓冲在此分配（三层头 + 载荷），fillTrans 填毕后沿实参链下传，fill 链自尾
    // 向前，至基座 move 收编。
    UDPDatagram(const MAC &etherDst, const MAC &etherSrc, const IPv4 srcIP, const IPv4 dstIP,
                const quint16 srcPort, const quint16 dstPort, const QByteArray &payload)
        : IPv4Packet(fillTrans(QByteArray(EtherFrame::HeaderSize + IPv4Packet::HeaderSize + HeaderSize +
                                              payload.size(),
                                          Qt::Uninitialized),
                               srcPort, dstPort, payload, srcIP, dstIP),
                     etherDst, etherSrc, srcIP, dstIP, Proto::UDP)
    {
    }

    // RX 收编口：kindOf 甄别为 UDPDatagram 后整帧入座（case 标签即类型担保）。
    explicit UDPDatagram(QByteArray frame) : IPv4Packet(std::move(frame)) {}

    // 载荷读口 bodyView：fromRawData 零拷贝——寿命随源帧字节，仅同步消费场景；收藏原样字节者须自深拷。
    //   返回 const（读口产物禁写申告：对返回临时量的非 const 访问 [detach 隐藏拷贝] 编译期封死；
    //   prvalue 直构返回槽保证省略照旧，COW 下弃 move 只费一次引用计数）。

    [[nodiscard]] const QByteArray bodyView() const
    {
        if (size() < kBodyOffset)
            return {};
        return QByteArray::fromRawData(cbegin() + kBodyOffset, bodyLen());
    }

    // 内核预滤谓词（叶口，公开）：本层无章参数，Proto::UDP 章自盖报入网络层；全字段
    // any-able——"哪些字段关心"是调用点的业务知识，本口纯机制。
    [[nodiscard]] static Filter matchUDP(const std::optional<MAC> &etherDst = {},
                                         const std::optional<MAC> &etherSrc = {},
                                         const std::optional<IPv4> &srcIP = {},
                                         const std::optional<IPv4> &dstIP = {},
                                         const std::optional<quint16> &srcPort = {},
                                         const std::optional<quint16> &dstPort = {})
    {
        return matchIPv4(Proto::UDP, etherDst, etherSrc, srcIP, dstIP) &
               Filter::match(kSrcPortOffset, srcPort) & Filter::match(kDstPortOffset, dstPort);
    }

private:
    // 端口两字居三层头起点，len 紧随，载荷段居本层头之后（字宽导出、链式推进——与 fillTrans
    // 顺序走笔同源；谓词与读口共用）。
    static constexpr int kSrcPortOffset = EtherFrame::HeaderSize + IPv4Packet::HeaderSize;
    static constexpr int kDstPortOffset = kSrcPortOffset + sizeof(quint16);
    static constexpr int kLenOffset     = kDstPortOffset + sizeof(quint16);
    static constexpr int kBodyOffset    = kSrcPortOffset + HeaderSize;

    // 载荷长 = len 字段 − 本层头（RFC768：字段含头长）。qBound 夹紧 [0, 帧内实际可用]：下界防
    // 坏 len（<8），上界防 len 谎报超帧；垫底字节因按字段裁天然出局（"不按帧长切"的兑现处）。
    [[nodiscard]] int bodyLen() const
    {
        const int fieldLen = qFromBigEndian<quint16>(cbegin() + kLenOffset) - HeaderSize;
        return qBound(0, fieldLen, static_cast<int>(size()) - kBodyOffset);
    }

    // 填本层段：四字头 + 载荷落位 + RFC768 校验和（伪首部取实参，载荷先落位再求和）——
    // 传入引用、填毕传出。
    [[nodiscard]] static QByteArray &&fillTrans(QByteArray &&frame, const quint16 srcPort, const quint16 dstPort,
                                                const QByteArray &payload, const IPv4 srcIP, const IPv4 dstIP)
    {
        auto *p = reinterpret_cast<unsigned char *>(frame.data()) + EtherFrame::HeaderSize + IPv4Packet::HeaderSize;
        const auto *const head = p;
        qToBigEndian(srcPort, p);
        p += sizeof(srcPort);
        qToBigEndian(dstPort, p);
        p += sizeof(dstPort);
        const auto len = static_cast<quint16>(HeaderSize + payload.size()); // len = 本层 + 载荷
        qToBigEndian(len, p);
        p += sizeof(len);
        auto *const slot = p; // 校验和占位（参与求和时须为零——RFC 768 规程）
        *p++ = 0;
        *p++ = 0;
        std::memcpy(p, payload.constData(), payload.size()); // 载荷先落位
        qToBigEndian(checksum(head, len, srcIP, dstIP), slot); // 全员落位后盖章
        return std::move(frame);
    }

    // RFC 768 校验和：伪首部 + 本层 + 载荷的 16 位反码和（求和时校验和字段为零）。
    // 伪首部 = srcIP / dstIP / Proto::UDP / len——取构造实参。
    [[nodiscard]] static quint16 checksum(const unsigned char *head, const quint16 len, const IPv4 srcIP,
                                          const IPv4 dstIP)
    {
        quint32 sum = 0;
        // 伪首部地址：IPv4 即字节序列，按 16 位字直读（经 uchar 视图——QByteArray 的 char 有符号，
        // 直接下标会符号扩展污染求和）。
        const auto *s = reinterpret_cast<const unsigned char *>(srcIP.constData());
        const auto *d = reinterpret_cast<const unsigned char *>(dstIP.constData());
        sum += quint32(s[0]) << 8 | s[1];
        sum += quint32(s[2]) << 8 | s[3];
        sum += quint32(d[0]) << 8 | d[1];
        sum += quint32(d[2]) << 8 | d[3];
        sum += static_cast<std::underlying_type_t<Proto>>(Proto::UDP); // 伪首部 protocol 字（零字节 + 协议号）
        sum += len;
        for (int i = 0; i < len - 1; i += 2)
            sum += static_cast<quint16>(head[i]) << 8 | head[i + 1];
        if (len & 1)
            sum += static_cast<quint16>(head[len - 1]) << 8; // 奇数长补零字节
        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<quint16>(~sum);
    }
};

// ------------------------------------------------------------
// RX 甄别口：收完整以太帧，读出帧种，消费方据以 switch。FrameKind 就是线上分用键
// （EtherFrame::Type → IPv4Packet::Proto / ARPPacket::Op）的复合读出——TX"种类分支自盖"
// 的 RX 对偶：盖章的逆操作是读章。长度门在此收口：各分支结构最短长不足即 Unknown——读章
// 与后续 case 体的字节读取自此安全（深校验如按 UDP 长度字段裁载荷归各读口）；不认识的
// 种类同归 Unknown → drop（枚举只命名本工程认识的种类）。无显式底型：底型 = 线上字段宽
// 的唯一声明点，本枚举不是线上字段。
// ------------------------------------------------------------
enum class FrameKind
{
    Unknown, // 不认识的种类 / 结构长度不足——drop 门
    ARPRequest,
    ARPReply,
    UDPDatagram,
};

[[nodiscard]] inline FrameKind kindOf(const QByteArray &frame)
{
    if (frame.size() < EtherFrame::HeaderSize)
        return FrameKind::Unknown;
    switch (EtherFrame::typeOf(frame))
    {
    case EtherFrame::Type::ARP:
        if (frame.size() < EtherFrame::HeaderSize + ARPPacket::BodySize)
            return FrameKind::Unknown; // 短于 ARP 定长本体（60B 物理垫底只会更长不会更短）
        switch (ARPPacket::opOf(frame))
        {
        case ARPPacket::Op::Request:
            return FrameKind::ARPRequest;
        case ARPPacket::Op::Reply:
            return FrameKind::ARPReply;
        default:
            return FrameKind::Unknown; // RARP 等不认识的 op
        }
    case EtherFrame::Type::IPv4:
        if (frame.size() < EtherFrame::HeaderSize + IPv4Packet::HeaderSize + UDPDatagram::HeaderSize ||
            IPv4Packet::protoOf(frame) != IPv4Packet::Proto::UDP)
            return FrameKind::Unknown; // 三层头结构最短长不足，或非 UDP 载荷
        return FrameKind::UDPDatagram;
    default:
        return FrameKind::Unknown; // 不在 Type 名单（VLAN/IPv6…）——枚举只命名认识的种类
    }
}

