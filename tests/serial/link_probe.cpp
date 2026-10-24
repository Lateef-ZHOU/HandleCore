#include <SerialHandle.h>
#include <QCoreApplication>
#include <cstdio>

struct Bytes
{
    void feed(const QByteArray &chunk, RxTime at, const DataCallback &deliver) { deliver(chunk, at); }
};
class RawSerial final : public SerialHandle<Bytes>
{
    Q_OBJECT
public:
    using SerialHandle::SerialHandle;
    ~RawSerial() override { stop(); }
Q_SIGNALS:
    void connectionChanged(bool) override;
    void logMessage(const QString &) const override;
private:
    RXRuling onRX(const QByteArray &raw, RxTime at, std::shared_ptr<ConnType>) override { return {RXRuling::Decode, raw, at}; }
    TXRuling onTX(const QByteArray &raw, std::shared_ptr<ConnType>) const override { return {TXRuling::Encode, raw}; }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    RawSerial serial([](const QByteArray &, RxTime) {});
    // 仅查询端口与库版本，不打开任何真实设备。
    const auto ports = availablePorts();
    std::printf("PASS real libserialport %s linked; %lld ports enumerated\n", sp_get_package_version_string(), static_cast<long long>(ports.size()));
}
#include "link_probe.moc"
