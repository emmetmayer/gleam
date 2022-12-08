#include "debug.h"
#include "fs.h"
#include "heap.h"
#include "render.h"
#include "physics_sandbox.h"
#include "timer.h"
#include "wm.h"
#include "physics.h"

#include "cpp_test.h"

int main(int argc, const char* argv[])
{
	debug_set_print_mask(k_print_info | k_print_warning | k_print_error);
	debug_install_exception_handler();

	timer_startup();
	debug_print(k_print_info, "%d\n", cpp_test_function(42));
	

	heap_t* heap = heap_create(2 * 1024 * 1024);
	fs_t* fs = fs_create(heap, 8);
	wm_window_t* window = wm_create(heap);
	render_t* render = render_create(heap, window);

	physics_sandbox_t* game = physics_sandbox_create(heap, fs, window, render, argc, argv);

	while (!wm_pump(window))
	{
		physics_sandbox_update(game);
	}

	/* XXX: Shutdown render before the game. Render uses game resources. */
	render_destroy(render);

	physics_sandbox_destroy(game);

	wm_destroy(window);
	fs_destroy(fs);
	heap_destroy(heap);

	return 0;
}
