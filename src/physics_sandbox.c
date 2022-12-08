#include "physics_sandbox.h"

#include "debug.h"
#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "net.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"
#include "physics.h"
#include "quatf.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

const float screen_size = 20.0f;

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct player_component_t
{
	int index;
	cpBody* body;
	cpShape* shape;
} player_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct physics_component_t
{
	cpBody* body;
	cpShape* shape;
} physics_component_t;

typedef struct physics_sandbox_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;
	net_t* net;
	cpSpace* physics_space;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int name_type;
	int physics_type;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t physics_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t cube_mesh;
	gpu_mesh_info_t hex_mesh;
	gpu_shader_info_t cube_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} physics_sandbox_t;

static void load_resources(physics_sandbox_t* game);
static void unload_resources(physics_sandbox_t* game);
static void spawn_player(physics_sandbox_t* game, int index);
static void spawn_cube(physics_sandbox_t* game, int index, vec3f_t size, vec3f_t pos, float angle, float friction, cpBodyType type);
static void spawn_circle(physics_sandbox_t* game, int index, float size, vec3f_t pos, float angle, float friction, cpBodyType type);
static void spawn_camera(physics_sandbox_t* game);
static void update_players(physics_sandbox_t* game);
static void update_physics(physics_sandbox_t* game);
static void draw_models(physics_sandbox_t* game);

physics_sandbox_t* physics_sandbox_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render, int argc, const char** argv)
{
	physics_sandbox_t* game = heap_alloc(heap, sizeof(physics_sandbox_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;
	game->physics_space = physicsSpaceCreate();
	physicsSpaceSetGravity(game->physics_space, cpv(0.0f, -10.0f));

	game->timer = timer_object_create(heap, NULL);

	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));
	game->physics_type = ecs_register_component_type(game->ecs, "physics", sizeof(physics_component_t), _Alignof(physics_component_t));


	game->net = net_create(heap, game->ecs);
	if (argc >= 2)
	{
		net_address_t server;
		if (net_string_to_address(argv[1], &server))
		{
			net_connect(game->net, &server);
		}
		else
		{
			debug_print(k_print_error, "Unable to resolve server address: %s\n", argv[1]);
		}
	}

	load_resources(game);
	spawn_player(game, 0);
	spawn_cube(game, 0, vec3f_new(2.0f, 2.0f, 0.0f), vec3f_new(5.0f, 5.0f, 0.0f), 0.0f, 1.0f, CP_BODY_TYPE_DYNAMIC);
	spawn_circle(game, 1, 2.0f, vec3f_new(20.0f, 9.0f, 0.0f), 0.0f, 1.0f, CP_BODY_TYPE_DYNAMIC);
	spawn_circle(game, 5, 20.0f, vec3f_new(50.0f, 9.0f, 0.0f), 0.0f, 1.0f, CP_BODY_TYPE_DYNAMIC);
	spawn_cube(game, 2, vec3f_new(80.0f, 1.0f, 0.0f), vec3f_new(0.0f, -40.0f, 0.0f), 0.0f , 1.0f, CP_BODY_TYPE_STATIC);
	spawn_cube(game, 3, vec3f_new(10.0f, 1.0f, 0.0f), vec3f_new(20.0f, -10.0f, 0.0f), 20.0f, 1.0f, CP_BODY_TYPE_STATIC);
	spawn_cube(game, 4, vec3f_new(10.0f, 1.0f, 0.0f), vec3f_new(0.0f, -20.0f, 0.0f), -20.0f, 1.0f, CP_BODY_TYPE_STATIC);
	
	spawn_camera(game);

	return game;
}

void physics_sandbox_destroy(physics_sandbox_t* game)
{
	cpSpaceDestroy(game->physics_space);
	net_destroy(game->net);
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void physics_sandbox_update(physics_sandbox_t* game)
{
	cpFloat timeStep = 1.0 / 60.0;
	cpSpaceStep(game->physics_space, timeStep);
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	net_update(game->net);
	update_players(game);
	update_physics(game);
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(physics_sandbox_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	game->cube_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -1.0f, -1.0f,  0.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f, -1.0f,  0.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f,  0.0f }, { 1.0f, 0.0f,  1.0f },
		{ -1.0f,  1.0f,  0.0f }, { 1.0f, 0.0f,  1.0f },

	};
	static uint16_t cube_indices[] =
	{
		2, 1, 0,
		0, 3, 2

	};

	static vec3f_t hex_verts[] =
	{
		{ -0.5f,  0.86f, 0.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f,  0.0f },
		{  0.5f,  0.86f, 0.0f }, { 1.0f, 0.0f,  0.0f },
		{ -0.5f, -0.86f, 0.0f }, { 1.0f, 0.0f,  0.0f },
		{  1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f,  0.0f },
		{  0.5f, -0.86f, 0.0f }, { 1.0f, 0.0f,  0.0f },
	};
	static uint16_t hex_indices[] =
	{
		2, 1, 0,
		2, 3, 1,
		4, 3, 2,
		4, 5, 3,
	};

	game->cube_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};

	game->hex_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = hex_verts,
		.vertex_data_size = sizeof(hex_verts),
		.index_data = hex_indices,
		.index_data_size = sizeof(hex_indices),
	};
}

static void unload_resources(physics_sandbox_t* game)
{
	heap_free(game->heap, fs_work_get_buffer(game->vertex_shader_work));
	heap_free(game->heap, fs_work_get_buffer(game->fragment_shader_work));
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void player_net_configure(ecs_t* ecs, ecs_entity_ref_t entity, int type, void* user)
{
	physics_sandbox_t* game = user;

	model_component_t* model_comp = ecs_entity_get_component(ecs, entity, game->model_type, true);
	model_comp->mesh_info = &game->cube_mesh;
	model_comp->shader_info = &game->cube_shader;
}

static void spawn_player(physics_sandbox_t* game, int index)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;
	player_comp->body = physicsRigidBodyCreate(game->physics_space, CP_BODY_TYPE_KINEMATIC, 1.0f, 1.0f, cpv(0.0f, 0.0f), 0.0f);
	player_comp->shape = physicsBoxCreate(game->physics_space, player_comp->body, 2.0f, 2.0f, 0.0f, 1.0f);

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->cube_mesh;
	model_comp->shader_info = &game->cube_shader;

	uint64_t k_player_ent_net_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->name_type);
	uint64_t k_player_ent_rep_mask =
		(1ULL << game->transform_type);
	net_state_register_entity_type(game->net, 0, k_player_ent_net_mask, k_player_ent_rep_mask, player_net_configure, game);

	net_state_register_entity_instance(game->net, 0, game->player_ent);
}

static void spawn_cube(physics_sandbox_t* game, int index, vec3f_t size, vec3f_t pos, float angle, float friction, cpBodyType type)
{
	uint64_t k_cube_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->physics_type) |
		(1ULL << game->name_type);
	game->physics_ent = ecs_entity_add(game->ecs, k_cube_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.scale = size;
	transform_comp->transform.translation = pos;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "cube");

	physics_component_t* physics_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->physics_type, true);
	physics_comp->body = physicsRigidBodyCreate(game->physics_space, type, size.x*size.y, 1.0f, cpv(pos.x, pos.y), angle);
	physics_comp->shape = physicsBoxCreate(game->physics_space, physics_comp->body, 2*size.x, 2*size.y, 0.0f, friction);

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->model_type, true);
	model_comp->mesh_info = &game->cube_mesh;
	model_comp->shader_info = &game->cube_shader;

	uint64_t k_cube_ent_net_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->name_type);
	uint64_t k_cube_ent_rep_mask =
		(1ULL << game->transform_type);
	net_state_register_entity_type(game->net, 0, k_cube_ent_net_mask, k_cube_ent_rep_mask, player_net_configure, game);

	net_state_register_entity_instance(game->net, 0, game->physics_ent);
}

static void spawn_circle(physics_sandbox_t* game, int index, float size, vec3f_t pos, float angle, float friction, cpBodyType type)
{
	uint64_t k_circle_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->physics_type) |
		(1ULL << game->name_type);
	game->physics_ent = ecs_entity_add(game->ecs, k_circle_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.scale.x = size;
	transform_comp->transform.scale.y = size;
	transform_comp->transform.translation = pos;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "circle");

	physics_component_t* physics_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->physics_type, true);
	physics_comp->body = physicsRigidBodyCreate(game->physics_space, type, pow((M_PI * size), 2.0f), 1.0f, cpv(pos.x, pos.y), angle);
	physics_comp->shape = physicsCircleCreate(game->physics_space, physics_comp->body, size, friction);

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->physics_ent, game->model_type, true);
	model_comp->mesh_info = &game->hex_mesh;
	model_comp->shader_info = &game->cube_shader;

	uint64_t k_circle_ent_net_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->name_type);
	uint64_t k_circle_ent_rep_mask =
		(1ULL << game->transform_type);
	net_state_register_entity_type(game->net, 0, k_circle_ent_net_mask, k_circle_ent_rep_mask, player_net_configure, game);

	net_state_register_entity_instance(game->net, 0, game->physics_ent);
}

static void spawn_camera(physics_sandbox_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	mat4f_make_orthographic(&camera_comp->projection, screen_size, 2.0f, -1000.0f, 1000.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static void update_players(physics_sandbox_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);

		transform_comp->transform.translation.x = (float)player_comp->body->p.x;
		transform_comp->transform.translation.y = (float)-player_comp->body->p.y;

		float vel_x = 0.0f;
		float vel_y = 0.0f;
		if (key_mask & k_key_up)
		{
			vel_y += 1.0f;
		}
		if (key_mask & k_key_down)
		{
			vel_y -= 1.0f;
		}
		if (key_mask & k_key_left)
		{
			vel_x += 1.0f;
		}
		if (key_mask & k_key_right)
		{
			vel_x -= 1.0f;
		}
		physicsRigidBodySetVelocity(player_comp->body, cpvmult(cpv(vel_x, vel_y), 10.0f));
	}
}

static void update_physics(physics_sandbox_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->physics_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		physics_component_t* physics_comp = ecs_query_get_component(game->ecs, &query, game->physics_type);

		transform_comp->transform.translation.x = (float)physics_comp->body->p.x;
		transform_comp->transform.translation.y = (float)-physics_comp->body->p.y;
		transform_comp->transform.rotation = quatf_from_eulers(vec3f_new(0.0f, 0.0f, -(float)physics_comp->body->a));
	}
}

static void draw_models(physics_sandbox_t* game)
{
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}
