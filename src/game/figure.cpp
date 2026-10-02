#include "figure.h"

#include "ei/hook.h"

void CAnimationNode::SetMorph(const BoneMorphBox* box, float s, float t, float u) {
    morphS_ = s;
    morphT_ = t;
    morphU_ = u;
    const Vector3* c = box->c;

    // Lower face: along c0-c1 by s, then towards c2 by t.
    Vector3 p;
    p.x = (c[1].x - c[0].x) * s + c[0].x;
    p.y = (c[1].y - c[0].y) * s + c[0].y;
    p.z = (c[1].z - c[0].z) * s + c[0].z;
    Vector3 q;
    q.Construct((c[2].x - p.x) * t, (c[2].y - p.y) * t, (c[2].z - p.z) * t);
    Vector3 lower;
    lower.x = q.x + p.x;
    lower.y = q.y + p.y;
    lower.z = q.z + p.z;

    // Upper face: along c3-c4 by s, then towards c5 by t.
    Vector3 edge, step, h, k, l;
    edge.Construct(c[4].x - c[3].x, c[4].y - c[3].y, c[4].z - c[3].z);
    step.Construct(edge.x * s, edge.y * s, edge.z * s);
    h.Construct(step.x + c[3].x, step.y + c[3].y, step.z + c[3].z);
    k.Construct(c[5].x - h.x, c[5].y - h.y, c[5].z - h.z);
    l.Construct(k.x * t, k.y * t, k.z * t);

    // Between the faces by u.
    Vector3 d;
    d.Construct((h.x + l.x) - lower.x, (l.y + h.y) - lower.y, (l.z + h.z) - lower.z);
    position_.x = d.x * u + lower.x;
    position_.y = d.y * u + lower.y;
    position_.z = d.z * u + lower.z;
}
EI_REPLACE_METHOD(0x005064C0, CAnimationNode::SetMorph);
