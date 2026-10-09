// The 4x4 matrix helpers of 004D9880-004D9DD0: the product, a quaternion's rotation matrix and the
// transform pipeline's matrix push. See model.h and docs/notes/script.md ("Leftovers").
#include "model.h"

#include "core_maths.h"
#include "ei/hook.h"

#include <cstring>

namespace {

// The order in which each product element sums its four terms a[r][k] * b[k][c], per column, for
// row 0 and for rows 1-3 (the original's instruction order). The partial sums go through RoundToFloat
// so that /fp:fast keeps that order; on the game's main thread (24-bit x87 precision) the stored
// partial sums equal the ones the original keeps on the FPU stack.
using SumOrder = std::uint8_t[2][4][4];
constexpr SumOrder kProductOrder = {
    {{0, 2, 1, 3}, {2, 0, 1, 3}, {0, 2, 1, 3}, {3, 1, 0, 2}},
    {{1, 3, 2, 0}, {1, 0, 3, 2}, {1, 3, 2, 0}, {0, 3, 2, 1}},
};
constexpr SumOrder kPushOrder = {
    {{3, 2, 1, 0}, {1, 0, 3, 2}, {0, 3, 1, 2}, {2, 1, 0, 3}},
    {{2, 1, 0, 3}, {0, 3, 2, 1}, {3, 2, 0, 1}, {3, 2, 1, 0}},
};

// out = a * b, element by element in memory order, reading the inputs again for each element.
void Multiply(float* out, const float* a, const float* b, const SumOrder& order) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const std::uint8_t* k = order[r ? 1 : 0][c];
            const float* row = a + r * 4;
            float sum = RoundToFloat(row[k[0]] * b[k[0] * 4 + c] + row[k[1]] * b[k[1] * 4 + c]);
            sum = RoundToFloat(sum + row[k[2]] * b[k[2] * 4 + c]);
            out[r * 4 + c] = sum + row[k[3]] * b[k[3] * 4 + c];
        }
    }
}

} // namespace

void __fastcall MatrixMultiply(float* out, void*, const float* a, const float* b) { Multiply(out, a, b, kProductOrder); }
EI_REPLACE(0x004D9880, MatrixMultiply);

void Quaternion::ToMatrix(float* out) const {
    // Twice the vector part; the products the original stores as floats go through RoundToFloat.
    float x2 = x + x;
    float y2 = RoundToFloat(y + y);
    float z2 = RoundToFloat(z + z);
    float wx = RoundToFloat(x2 * w);
    float wy = RoundToFloat(y2 * w);
    float wz = RoundToFloat(z2 * w);
    float xx = x2 * x;
    float xy = y2 * x;
    float xz = RoundToFloat(z2 * x);
    float yy = RoundToFloat(y2 * y);
    float yz = RoundToFloat(z2 * y);
    float zz = RoundToFloat(z2 * z);
    out[0] = 1.0f - RoundToFloat(zz + yy);
    out[1] = xy - wz;
    out[2] = xz + wy;
    out[4] = xy + wz;
    out[5] = 1.0f - RoundToFloat(zz + xx);
    out[6] = yz - wx;
    out[8] = xz - wy;
    out[9] = yz + wx;
    out[10] = 1.0f - RoundToFloat(yy + xx);
}
EI_REPLACE_METHOD(0x004D9AA0, Quaternion::ToMatrix);

void CTransformPipeline::PushMatrix(const float* m, int index) {
    MatrixStack& stack = stacks_[index];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices, m, sizeof(Matrix4));
        return;
    }
    ++stack.count;
    Multiply(stack.matrices[stack.count - 1].m, stack.matrices[stack.count - 2].m, m, kPushOrder);
}
EI_REPLACE_METHOD(0x004D9B80, CTransformPipeline::PushMatrix);
