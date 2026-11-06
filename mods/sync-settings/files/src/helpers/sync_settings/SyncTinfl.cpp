// The portable raw-DEFLATE decoder for chips without miniz in mask ROM; ESP32 builds compile nothing here.
#if defined(NRF52_PLATFORM)
#include "miniz/miniz_tinfl.c.inc"
#endif
