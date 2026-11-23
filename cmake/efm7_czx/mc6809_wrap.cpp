// Wrapper providing a stub `osd` global required by upstream CSCP when _MSC_VER is not defined.
#include "common.h"
#include "sdl/osd.h"

namespace {
OSD g_stub_osd;
OSD* const osd = &g_stub_osd;
}  // namespace

#include "../../third_party/takeda-cscp-emulators/src/vm/mc6809.cpp"
