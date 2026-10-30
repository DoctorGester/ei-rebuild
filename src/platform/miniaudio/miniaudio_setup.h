#pragma once

// miniaudio (third_party/miniaudio, unmodified) as the audio backend builds it: the device and the dr_wav / dr_mp3
// decoders, without the parts it does not use (the engine, the resource manager, the node graph, encoders, FLAC).
// Every file that includes miniaudio.h includes it through here, so its structures have the same layout everywhere.
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION
#define MA_NO_ENCODING
#define MA_NO_FLAC
#pragma warning(push, 0)
#include "miniaudio/miniaudio.h"
#pragma warning(pop)
