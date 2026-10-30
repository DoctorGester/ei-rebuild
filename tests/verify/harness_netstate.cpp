// The "netstate" step: the sessions' state as console lines (harness_pair.h, SV5d4).
#include "harness_pair.h"

#include "image/image_side.h"

namespace ei::verify::pair {

std::vector<std::string> NetState() { return image::NetState(); }

} // namespace ei::verify::pair
