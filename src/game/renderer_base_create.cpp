// The first step of the scene renderer's creation (CVisualizator::Create in the game's messages): makes
// the transform pipeline. (The MMX check and 2DintMMX.dll are the device object's: device_object.cpp.)
#include "model.h"

#include "options.h"

#include <string>

void CSceneRenderer::CreatePipeline(const options::GameOptions* settings) {
    CTransformPipeline* pipeline = new CTransformPipeline(settings->workPath_.c_str());   // 0x94 bytes
    CTransformPipeline* old = pipeline_;
    pipeline_ = pipeline;
    if (pipeline) ++pipeline->refs_;
    if (old) old->Release();
}
