#include "physics.h"
///Space Functions
cpSpace* physicsSpaceCreate()
{
	cpSpace* space = cpSpaceNew();
	return space;
}

void physicsSpaceDestroy(cpSpace* space)
{
	cpSpaceFree(space);
}

void physicsSpaceSetGravity(cpSpace* space, cpVect gravity)
{
	cpSpaceSetGravity(space, gravity);
}

cpBody* physicsSpaceGetStaticBody(cpSpace* space)
{
	return cpSpaceGetStaticBody(space);
}

///Rigidbody Functions
cpBody* physicsRigidBodyCreate(cpSpace* space, cpBodyType type, cpFloat mass, cpFloat moment, cpVect pos)
{
	cpBody* body = cpSpaceAddBody(space, cpBodyNew(0.0f, 0.0f));
	cpBodySetType(body, type);
	cpBodySetPosition(body, pos);
	if (cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC)
	{
		cpBodySetMass(body, mass);
		cpBodySetMoment(body, moment);
	}
	return body;
}

void physicsRigidBodyDestroy(cpBody* body)
{
	cpBodyFree(body);
}

void physicsRigidBodySetVelocity(cpBody* body, cpVect velocity)
{
	cpBodySetVelocity(body, velocity);
}

///Shape Functions
cpShape* physicsCircleCreate(cpSpace* space, cpBody* body, cpFloat radius, cpFloat friction)
{
	cpShape* circle = cpSpaceAddShape(space, cpCircleShapeNew(body, radius, cpvzero));
	cpShapeSetFriction(circle, friction);
	return circle;
}

cpShape* physicsBoxCreate(cpSpace* space, cpBody* body, cpFloat width, cpFloat height, cpFloat radius, cpFloat friction)
{
	cpShape* box = cpSpaceAddShape(space, cpBoxShapeNew(body, width, height, radius));
	cpShapeSetFriction(box, friction);
	return box;
}

void physicsShapeDestroy(cpShape* shape)
{
	cpShapeFree(shape);
}