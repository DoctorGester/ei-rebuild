// The game CRT's locale and multibyte state (crt_locale_data.h): game.exe's .data / .bss objects, named (C18i).
#include "crt_locale_data.h"

namespace libcrt {

unsigned g_lcHandle[6] = /*007CABE8*/ {};
unsigned g_lcCodePage = /*007CAC00*/ {};
unsigned g_lcCollateCodePage = /*007CAC04*/ {};
std::uint16_t* g_ctypeBlock = /*007CAC28*/ {nullptr};
std::uint16_t* g_wideCtypeBlock = /*007CAC2C*/ {nullptr};
LocaleId g_lcId[6] = /*007CAC30*/ {};
int g_mbCurMax = /*00797FE4*/ {1};
char g_decimalPoint[2] = /*00797FE8*/ {"."};

char g_cacheIn[132] = /*0079777C*/ {"C"};
char g_cacheOut[132] = /*00797800*/ {"C"};
LocaleId g_cachedId = /*007CAB24*/ {};
unsigned g_cachedCodePage = /*007CAB2C*/ {};

unsigned g_mbCodePage = /*007CC058*/ {};
unsigned g_mbUlInfo[3] = /*007CC060*/ {};
int g_isMbCodePage = /*007CC06C*/ {};
std::uint8_t g_mbCaseMap[256] = /*007CC080*/ {};
std::uint8_t g_mbCtype[257] = /*007CC180*/ {};
unsigned g_mbLcid = /*007CC284*/ {};
int g_mbTableReady = /*007CC28C*/ {};
int g_mbSystemSet = /*007CAB20*/ {};

} // namespace libcrt
