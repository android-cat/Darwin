#pragma once

#include <QRecursiveMutex>

namespace Darwin {

QRecursiveMutex& modelAccessMutex();

}
