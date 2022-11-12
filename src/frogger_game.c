#include "frogger_game.h"

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

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

const float player_speed = 5.0f;
const float truck_speed = 6.0f;
const float screen_height = 20.0f;

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
} player_component_t;

typedef struct lane_component_t
{
	int index;
	int direction;
} lane_component_t;

typedef struct truck_component_t
{
	int index;
	int direction;
} truck_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct frogger_game_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;
	net_t* net;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int lane_type;
	int truck_type;
	int name_type;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t lane_ent;
	ecs_entity_ref_t truck_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t player_mesh;
	gpu_mesh_info_t truck1_mesh;
	gpu_mesh_info_t truck2_mesh;
	gpu_mesh_info_t truck3_mesh;
	gpu_shader_info_t cube_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} frogger_game_t;

static void load_resources(frogger_game_t* game);
static void unload_resources(frogger_game_t* game);
static void spawn_player(frogger_game_t* game, int index);
static void spawn_lane(frogger_game_t* game, int index, int direction, vec3f_t position);
static void spawn_truck(frogger_game_t* game, int index, int direction, vec3f_t position, float size, gpu_mesh_info_t* mesh);
static void spawn_camera(frogger_game_t* game);
static void update_players(frogger_game_t* game);
static void update_trucks(frogger_game_t* game);
static void draw_models(frogger_game_t* game);

frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render, int argc, const char** argv)
{
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);

	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->truck_type = ecs_register_component_type(game->ecs, "truck", sizeof(truck_component_t), _Alignof(truck_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));

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
	spawn_lane(game, 0, 1, vec3f_new(0.0f, 0.0f, 12.0f));
	spawn_lane(game, 1, -1, vec3f_new(0.0f, 0.0f, 4.0f));
	spawn_lane(game, 2, 1, vec3f_new(0.0f, 0.0f, -4.0f));
	spawn_lane(game, 3, -1, vec3f_new(0.0f, 0.0f, -12.0f));
	spawn_camera(game);

	return game;
}

void frogger_game_destroy(frogger_game_t* game)
{
	net_destroy(game->net);
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game)
{
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	net_update(game->net);
	update_players(game);
	update_trucks(game);
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(frogger_game_t* game)
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

	static vec3f_t player_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.36863f,  0.86667f,  0.37255f},
		{  1.0f, -1.0f,  1.0f }, { 0.36863f,  0.86667f,  0.37255f},
		{  1.0f,  1.0f,  1.0f }, { 0.36863f,  0.86667f,  0.37255f},
		{ -1.0f,  1.0f,  1.0f }, { 0.36863f,  0.86667f,  0.37255f},
		{ -1.0f, -1.0f, -1.0f }, { 0.36863f,  0.86667f,  0.37255f},
		{  1.0f, -1.0f, -1.0f }, { 0.36863f,  0.86667f,  0.37255f},
		{  1.0f,  1.0f, -1.0f }, { 0.36863f,  0.86667f,  0.37255f},
		{ -1.0f,  1.0f, -1.0f }, { 0.36863f,  0.86667f,  0.37255f},
	};
	static vec3f_t truck1_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f, 1.0f},
		{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f, 1.0f},
		{  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f, 1.0f},
		{ -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f, 1.0f},
		{ -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 1.0f},
		{  1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 1.0f},
		{  1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f, 1.0f},
		{ -1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f, 1.0f},
	};
	static vec3f_t truck2_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f, 0.0f},
		{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f, 0.0f},
		{  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f, 0.0f},
		{ -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f, 0.0f},
		{ -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f},
		{  1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f},
		{  1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f},
		{ -1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f},
	};
	static vec3f_t truck3_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f, 1.0f},
		{  1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f, 1.0f},
		{  1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f, 1.0f},
		{ -1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f, 1.0f},
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f},
		{  1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f},
		{  1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f},
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f},
	};
	static uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};
	game->player_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = player_verts,
		.vertex_data_size = sizeof(player_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->truck1_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = truck1_verts,
		.vertex_data_size = sizeof(truck1_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->truck2_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = truck2_verts,
		.vertex_data_size = sizeof(truck2_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
	game->truck3_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = truck3_verts,
		.vertex_data_size = sizeof(truck3_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};

}

static void unload_resources(frogger_game_t* game)
{
	heap_free(game->heap, fs_work_get_buffer(game->vertex_shader_work));
	heap_free(game->heap, fs_work_get_buffer(game->fragment_shader_work));
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void player_net_configure(ecs_t* ecs, ecs_entity_ref_t entity, int type, void* user)
{
	frogger_game_t* game = user;

	model_component_t* model_comp = ecs_entity_get_component(ecs, entity, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->cube_shader;
}

static void truck_net_configure(ecs_t* ecs, ecs_entity_ref_t entity, int type, void* user)
{
	frogger_game_t* game = user;

	model_component_t* model_comp = ecs_entity_get_component(ecs, entity, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->cube_shader;
}

static void spawn_player(frogger_game_t* game, int index)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.z = 18.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
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

static void spawn_lane(frogger_game_t* game, int index, int direction, vec3f_t position)
{
	uint64_t k_lane_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->lane_type) |
		(1ULL << game->name_type);
	game->lane_ent = ecs_entity_add(game->ecs, k_lane_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->lane_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation = position;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->lane_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "lane");

	lane_component_t* lane_comp = ecs_entity_get_component(game->ecs, game->lane_ent, game->lane_type, true);
	lane_comp->index = index;
	lane_comp->direction = direction;

	uint64_t k_lane_ent_net_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->name_type);
	uint64_t k_lane_ent_rep_mask =
		(1ULL << game->transform_type);
	net_state_register_entity_type(game->net, 0, k_lane_ent_net_mask, k_lane_ent_rep_mask, player_net_configure, game);

	net_state_register_entity_instance(game->net, 0, game->lane_ent);
	if (index == 0)
	{
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -36.0f, position.z), 4, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -23.0f, position.z), 3, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -8.0f, position.z), 2, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 6.0f, position.z), 6, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 23.0f, position.z), 3, &game->truck3_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 34.0f, position.z), 2, &game->truck2_mesh);
	}
	else if (index == 1)
	{
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -38.0f, position.z), 2, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -29.0f, position.z), 3, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -15.0f, position.z), 3, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -2.0f, position.z), 4, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 10.0f, position.z), 2, &game->truck3_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 20.0f, position.z), 4, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 35.0f, position.z), 5, &game->truck3_mesh);
	}
	else if (index == 2)
	{
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -34.0f, position.z), 4, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -17.0f, position.z), 5, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -4.0f, position.z), 2, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 6.0f, position.z), 4, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 21.0f, position.z), 3, &game->truck3_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 34.0f, position.z), 6, &game->truck2_mesh);
	}
	else if (index == 3)
	{
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -38.0f, position.z), 2, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -24.0f, position.z), 6, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, -7.0f, position.z), 3, &game->truck1_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 8.0f, position.z), 4, &game->truck2_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 23.0f, position.z), 5, &game->truck3_mesh);
		spawn_truck(game, 0, direction, vec3f_new(0.0f, 36.0f, position.z), 4, &game->truck2_mesh);
	}

}

static void spawn_truck(frogger_game_t* game, int index, int direction, vec3f_t position, float size, gpu_mesh_info_t* mesh)
{
	uint64_t k_truck_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->truck_type) |
		(1ULL << game->name_type);
	game->truck_ent = ecs_entity_add(game->ecs, k_truck_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->truck_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation = position;
	transform_comp->transform.scale.z = 2;
	transform_comp->transform.scale.y = size;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->truck_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "truck");

	truck_component_t* truck_comp = ecs_entity_get_component(game->ecs, game->truck_ent, game->truck_type, true);
	truck_comp->index = index;
	truck_comp->direction = direction;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->truck_ent, game->model_type, true);
	model_comp->mesh_info = mesh;
	model_comp->shader_info = &game->cube_shader;

	uint64_t k_truck_ent_net_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->name_type);
	uint64_t k_truck_ent_rep_mask =
		(1ULL << game->transform_type);
	net_state_register_entity_type(game->net, 0, k_truck_ent_net_mask, k_truck_ent_rep_mask, player_net_configure, game);

	net_state_register_entity_instance(game->net, 0, game->truck_ent);
}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	mat4f_make_orthographic(&camera_comp->projection, screen_height, 2.0f, -1000.0f, 1000.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static void update_players(frogger_game_t* game)
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

		if (transform_comp->transform.translation.z < -19.0f)
		{
			transform_comp->transform.translation.z = 18.0f;
			transform_comp->transform.translation.y = 0.0f;
		}
		k_query_mask = (1ULL << game->transform_type) | (1ULL << game->truck_type);

		for (ecs_query_t truck_query = ecs_query_create(game->ecs, k_query_mask);
			ecs_query_is_valid(game->ecs, &truck_query);
			ecs_query_next(game->ecs, &truck_query))
		{
			transform_component_t* transform_truck = ecs_query_get_component(game->ecs, &truck_query, game->transform_type);
			
			if (
				transform_comp->transform.translation.y < transform_truck->transform.translation.y + (transform_truck->transform.scale.y * 1.25) &&
				transform_comp->transform.translation.y + (transform_comp->transform.scale.y * 1.25) > transform_truck->transform.translation.y &&
				transform_comp->transform.translation.z < transform_truck->transform.translation.z + (transform_truck->transform.scale.z * 1.5f) &&
				(transform_comp->transform.scale.z * 1.5f) + transform_comp->transform.translation.z > transform_truck->transform.translation.z
				)
			{
				transform_comp->transform.translation.z = 18.0f;
				transform_comp->transform.translation.y = 0.0f;
			}
		}
		transform_t move;
		transform_identity(&move);
		if (key_mask & k_key_up)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt * player_speed));
		}
		if (key_mask & k_key_down)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt * player_speed));
		}
		if (key_mask & k_key_left)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt * player_speed));
		}
		if (key_mask & k_key_right)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt * player_speed));
		}
		transform_multiply(&transform_comp->transform, &move);
	}
}

static void update_trucks(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->truck_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		truck_component_t* truck_comp = ecs_query_get_component(game->ecs, &query, game->truck_type);

		if ((truck_comp->direction == 1 && transform_comp->transform.translation.y > (40.0f + transform_comp->transform.scale.y)) || (truck_comp->direction == -1 && transform_comp->transform.translation.y < (-40.0f - transform_comp->transform.scale.y)))
		{
			transform_comp->transform.translation.y = (40.0f + transform_comp->transform.scale.y) * -truck_comp->direction;
		}
		else
		{
			transform_t move;
			transform_identity(&move);
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt * truck_speed * truck_comp->direction));
			transform_multiply(&transform_comp->transform, &move);
		}
		
	}
}

static void draw_models(frogger_game_t* game)
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