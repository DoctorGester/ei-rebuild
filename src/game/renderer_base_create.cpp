// The first step of the scene renderer's creation (CVisualizator::Create in the game's messages): makes
// the transform pipeline.
#include "model.h"

void SceneRenderer::CreatePipeline() { pipeline.Assign(new TransformPipeline()); }
