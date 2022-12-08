#include "chipmunk/chipmunk_private.h"
///Space Functions
cpSpace* physicsSpaceCreate();

void physicsSpaceDestroy(cpSpace* space);

void physicsSpaceSetGravity(cpSpace* space, cpVect gravity);

cpBody* physicsSpaceGetStaticBody(cpSpace* space);

///Rigidbody Functions
cpBody* physicsRigidBodyCreate(cpSpace* space, cpBodyType type, cpFloat mass, cpFloat moment, cpVect pos, cpFloat angle);

void physicsRigidBodyDestroy(cpBody* body);

void physicsRigidBodySetVelocity(cpBody* body, cpVect velocity);

///Shape Functions
cpShape* physicsCircleCreate(cpSpace* space, cpBody* body, cpFloat radius, cpFloat friction);

cpShape* physicsBoxCreate(cpSpace* space, cpBody* body, cpFloat width, cpFloat height, cpFloat radius, cpFloat friction);

void physicsShapeDestroy(cpShape* shape);