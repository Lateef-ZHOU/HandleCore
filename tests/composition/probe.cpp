#include "First.h"
#include "Second.h"
#include <cstring>

int main()
{
    FirstConsumer first;
    SecondConsumer second;
    return std::strcmp(first.metaObject()->className(), "FirstConsumer") != 0
        || std::strcmp(second.metaObject()->className(), "SecondConsumer") != 0;
}
