// The first step of the scene renderer's creation (CVisualizator::Create in the game's messages): makes
// the transform pipeline.
#include "model.h"

#include "options.h"

#include <string>

void CSceneRenderer::CreatePipeline(const options::GameOptions* settings) {
    pipeline_.Assign(new CTransformPipeline(settings->workPath_.c_str()));   // 0x94 bytes
}
