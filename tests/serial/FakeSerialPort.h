#pragma once
#include <QByteArray>
#include <vector>
#include <libserialport.h>

// 测试链接替身：生产 SerialHandle/SerialConn 原样调用真实头文件声明的 C API。
namespace FakeSerialPort
{
void reset();
void enqueue(QByteArray bytes);
void failRead();
void failOpen(bool fail);
void failConfiguration(bool fail);
void writeResult(int result);
void blockWrites(bool blocked);
int opens();
int closes();
int livePorts();
int reads();
int writesStarted();
int maxConcurrentWrites();
int baudRate();
sp_flowcontrol flowControl();
unsigned writeTimeout();
std::vector<QByteArray> writes();
}
