#include "ModelAccessLock.h"

namespace Darwin {

QRecursiveMutex& modelAccessMutex()
{
    static QRecursiveMutex mutex;
    return mutex;
}

}
