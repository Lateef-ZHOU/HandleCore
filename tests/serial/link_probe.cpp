#include <functional>
#include "../CallbackHandle.h"
#include <SerialHandle.h>
#include <QCoreApplication>
#include <cstdio>

struct Bytes
{
    void feed(const QByteArray &chunk, RxTime at, const std::function<void(const QByteArray &, RxTime)> &deliver) { deliver(chunk, at); }
};
class RawSerial : public SerialHandle<Bytes>
{
    Q_OBJECT
public:
    ~RawSerial() override = default;
Q_SIGNALS:
    void connectionChanged(HandleCore::ResourcePhase) override;
    void logMessage(const QString &) const override;
private:
    RXRuling onRX(const QByteArray &raw, RxTime at, std::shared_ptr<SerialConn<Bytes>>) override { return {raw, RXRuling::Decode, at}; }
    TXRuling onTX(const QByteArray &raw, std::shared_ptr<SerialConn<Bytes>>) const override { return {raw, TXRuling::Encode}; }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    CallbackHandle<RawSerial> serial([](const QByteArray &, RxTime) {});
    // 仅查询端口与库版本，不打开任何真实设备。
    const auto ports = availablePorts();
    std::printf("PASS real libserialport %s linked; %lld ports enumerated\n", sp_get_package_version_string(), static_cast<long long>(ports.size()));
}
#include "link_probe.moc"
