/* uGreen -> uWSGI green threads */

#include <uwsgi.h>

#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#include <ucontext.h>

struct uwsgi_ugreen {
	int             ugreen;
        int             stackpages;
        ucontext_t      main;
        ucontext_t    *contexts;
        size_t          u_stack_size;
} ug;

#define UGREEN_DEFAULT_STACKSIZE 256*1024


extern struct uwsgi_server uwsgi;

struct uwsgi_option ugreen_options[] = {
	{"ugreen", no_argument, 0, "enable ugreen coroutine subsystem", uwsgi_opt_true, &ug.ugreen, 1},
	{"ugreen-stacksize", required_argument, 0, "set ugreen stack size in pages", uwsgi_opt_set_int, &ug.stackpages, 0},
	{ 0, 0, 0, 0, 0, 0, 0 }
};

void u_green_request() {
	uwsgi.wsgi_req->async_status = uwsgi.p[uwsgi.wsgi_req->uh->modifier1]->request(uwsgi.wsgi_req);
	uwsgi.wsgi_req->suspended = 0;
}

static void u_green_schedule_to_req() {

	int id = uwsgi.wsgi_req->async_id;

	if (!uwsgi.wsgi_req->suspended) {
		ug.contexts[id].uc_link = &ug.main;
        	makecontext(&ug.contexts[id], u_green_request, 0);
		uwsgi.wsgi_req->suspended = 1;
	}

	if (uwsgi.p[uwsgi.wsgi_req->uh->modifier1]->suspend) {
		uwsgi.p[uwsgi.wsgi_req->uh->modifier1]->suspend(NULL);
	}

	swapcontext(&ug.main, &ug.contexts[id] );		

	if (uwsgi.p[uwsgi.wsgi_req->uh->modifier1]->resume) {
		uwsgi.p[uwsgi.wsgi_req->uh->modifier1]->resume(NULL);
	}

	if (uwsgi.wsgi_req->suspended) {
		uwsgi.wsgi_req->async_status = UWSGI_AGAIN;
	}

}

static void u_green_schedule_to_main(struct wsgi_request *wsgi_req) {

	if (uwsgi.p[wsgi_req->uh->modifier1]->suspend) {
		uwsgi.p[wsgi_req->uh->modifier1]->suspend(wsgi_req);
	}

	swapcontext(&ug.contexts[wsgi_req->async_id], &ug.main);

	if (uwsgi.p[wsgi_req->uh->modifier1]->resume) {
		uwsgi.p[wsgi_req->uh->modifier1]->resume(wsgi_req);
	}

	uwsgi.wsgi_req = wsgi_req;
}


int u_green_init() {

	static int i;

	if (!ug.ugreen) {
		return 0;
	}

	ug.u_stack_size = UGREEN_DEFAULT_STACKSIZE;

	if (ug.stackpages > 0) {
		ug.u_stack_size = ug.stackpages * uwsgi.page_size;
	}

	uwsgi_log("initializing %d uGreen threads with stack size of %lu (%lu KB)\n", uwsgi.async, (unsigned long) ug.u_stack_size,  (unsigned long) ug.u_stack_size/1024);


	ug.contexts = uwsgi_malloc( sizeof(ucontext_t) * uwsgi.async);


	for(i=0;i<uwsgi.async;i++) {

		getcontext(&ug.contexts[i]);

		ug.contexts[i].uc_stack.ss_sp = mmap(NULL, ug.u_stack_size + (uwsgi.page_size*2) , PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0) + uwsgi.page_size;

		if (!ug.contexts[i].uc_stack.ss_sp) {
			uwsgi_error("mmap()");
			exit(1);
		}
		// set guard pages for stack
		if (mprotect(ug.contexts[i].uc_stack.ss_sp - uwsgi.page_size, uwsgi.page_size, PROT_NONE)) {
			uwsgi_error("mprotect()");
			exit(1);
		}
		if (mprotect(ug.contexts[i].uc_stack.ss_sp + ug.u_stack_size, uwsgi.page_size, PROT_NONE)) {
			uwsgi_error("mprotect()");
			exit(1);
		}

		ug.contexts[i].uc_stack.ss_size = ug.u_stack_size;

	}


	uwsgi.schedule_to_main = u_green_schedule_to_main;
	uwsgi.schedule_to_req = u_green_schedule_to_req;

	return 0;

}

struct uwsgi_plugin ugreen_plugin = {

	.name = "ugreen",
	.init = u_green_init,
	.options = ugreen_options,
};
