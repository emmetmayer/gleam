#include "physics.h"
#define _USE_MATH_DEFINES
#include <math.h>

const float R2D = (float)(180.0f / M_PI);
///Space Functions
///Return an allocated physics space
cpSpace* physicsSpaceCreate()
{
	cpSpace* space = cpSpaceNew();
	return space;
}
///Destroy and free a passed in physics space
void physicsSpaceDestroy(cpSpace* space)
{
	cpSpaceFree(space);
}
///Set the gravity of a physics space
void physicsSpaceSetGravity(cpSpace* space, cpVect gravity)
{
	cpSpaceSetGravity(space, gravity);
}
///Return the rigidbody allocated for static shapes in the space
cpBody* physicsSpaceGetStaticBody(cpSpace* space)
{
	return cpSpaceGetStaticBody(space);
}

///Rigidbody Functions
///Return an allocated rigidbody of dynamic kinematic or static type with mass moment a position and rotation
cpBody* physicsRigidBodyCreate(cpSpace* space, cpBodyType type, cpFloat mass, cpFloat moment, cpVect pos, cpFloat angle)
{
	cpBody* body = cpSpaceAddBody(space, cpBodyNew(0.0f, 0.0f));
	cpBodySetType(body, type);
	cpBodySetPosition(body, pos);
	cpBodySetAngle(body, angle/R2D);
	if (cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC)
	{
		cpBodySetMass(body, mass);
		cpBodySetMoment(body, moment);
	}
	return body;
}
///Destroy and free a rigidbody
void physicsRigidBodyDestroy(cpBody* body)
{
	cpBodyFree(body);
}

///Absoulutely set the velocity of a rigidbody, used for player movement with kinematic bodies
void physicsRigidBodySetVelocity(cpBody* body, cpVect velocity)
{
	cpBodySetVelocity(body, velocity);
}

///Shape Functions
///Return an allocated circle shape attached to a rigidbody in a space with a radius and friction coeff
cpShape* physicsCircleCreate(cpSpace* space, cpBody* body, cpFloat radius, cpFloat friction)
{
	cpShape* circle = cpSpaceAddShape(space, cpCircleShapeNew(body, radius, cpvzero));
	cpShapeSetFriction(circle, friction);
	return circle;
}
///Return an allocated box shape attached to a rigidbody in a space with a width, height, and friction coeff
cpShape* physicsBoxCreate(cpSpace* space, cpBody* body, cpFloat width, cpFloat height, cpFloat radius, cpFloat friction)
{
	cpShape* box = cpSpaceAddShape(space, cpBoxShapeNew(body, width, height, radius));
	cpShapeSetFriction(box, friction);
	return box;
}
///Destroy and free a shape
void physicsShapeDestroy(cpShape* shape)
{
	cpShapeFree(shape);
}