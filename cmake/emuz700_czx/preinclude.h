#ifndef CZX_EMUZ700_PREINCLUDE_H
#define CZX_EMUZ700_PREINCLUDE_H

#include "common.h"

template <size_t N>
inline errno_t my_tcscpy_s(_TCHAR (&strDestination)[N], const _TCHAR* strSource) {
    return my_tcscpy_s(strDestination, N, strSource);
}

inline size_t min(size_t a, uint32_t b) {
    return (a < static_cast<size_t>(b)) ? a : static_cast<size_t>(b);
}

#endif
