/* The function that the planted call reaches (include/types.h says what this folder is for). Like the game's
 * poseEntityAtAnimFrame: vec, when it is not NULL, holds two look angles, which the function reads. */
#include "types.h"

f32 gPose[3];
f32 gLook[2];

void poseModel(s16 entity, s16 anim, s16 frame, f32 *pos, f32 *vec, u8 flags) {
    gPose[0] = pos[0] + entity;
    gPose[1] = pos[1] + anim;
    gPose[2] = pos[2] + frame;
    if (vec != 0) {
        gLook[0] = vec[0];
        gLook[1] = vec[1];
    }
    if (flags & 1) {
        gPose[0] = 0.0f;
    }
}
