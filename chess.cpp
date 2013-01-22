#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <dlfcn.h>
#include <map>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

using namespace std;

#define DEBUG 0

int (*original_pthread_create)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*) = NULL;
int (*original_pthread_join)(pthread_t, void**) = NULL;
int (*original_pthread_mutex_lock)(pthread_mutex_t*) = NULL;
int (*original_pthread_mutex_unlock)(pthread_mutex_t*) = NULL;

static void initialize_original_functions();
static bool gl_is_init = false;
static bool initialized = false;
static pthread_mutex_t global_lock;
static unsigned int running_thread;

/* This implementation is only compatible with programs
 * that use 2 threads. It can be easily changed by adding
 * additional t#_id variables or (probably better) switching
 * this tool to use an array of IDs instead of multiple ints.
 */
int t1_id = -1;
int t2_id = -1;

/* Since this implementation only assumes 2 threads, master_id
 * is an XOR of the two thread IDs. This is used when switching
 * between threads at a context switch. An XOR between the current
 * thread's ID and the master_id will give the ID of the next thread
 * to switch to.
 */
unsigned int master_id = 0;

/* explore is true if we are exploring the execution of the program
 * this tool is testing. If so, we keep count of how many times we
 * attempt to lock a mutex. We write this final number to file
 * after execution. We then rerun the program count number of times
 * with explore set to false. We then force a context switch when
 * switch_count is equal to the current execution number.
 */
static bool explore = true;
static FILE * file;
static int switch_count = 0;
static int switch_index;


struct Thread_Arg {
    void* (*start_routine)(void*);
    void* arg;
};

enum thread_status{
        running = 0,
        waiting = 1,
        terminated = 2
    };

struct Thread_Node{
    Thread_Node *next;
    thread_status status;
    pthread_t id;
};

struct Mutex_Node{
    Mutex_Node *next;
    bool is_held;
    pthread_t holding_thread;
    pthread_mutex_t * mutex;
};

Thread_Node * thread_head = NULL;
Mutex_Node * mutex_head = NULL;

/* Takes a thread and returns the Thread_Node
 * in the Thread_Node linked list that contains it.
 */
Thread_Node *get_thread_node(pthread_t id){
    Thread_Node *current = thread_head;
    while(current->next){
        if(current->next->id == id){
            return current->next;
        }
        current = current->next;
    }
    return NULL;
}

/* Inserts a Thread_Node at the end of the
 * Thread_Node linked list
 */
void insert_thread_node(Thread_Node *node){
    Thread_Node *current = thread_head;
    while(current->next){
        current = current->next;
    }
    current->next = node;
}

/* Takes a pointer to a mutex and returns the Mutex_Node
 * in the Mutex_Node linked list that contains it.
 */
Mutex_Node *get_mutex_node(pthread_mutex_t *mutex){
    Mutex_Node *current = mutex_head;
    while(current->next){
        if(current->next->mutex == mutex){
            return current->next;
        }
        current = current->next;
    }
    return NULL;
}

/* Inserts a Mutex_Node at the end of the
 * Mutex_Node linked list
 */
void insert_mutex_node(Mutex_Node *node){
    Mutex_Node *current = mutex_head;
    while(current->next){
        current = current->next;
    }
    current->next = node;
}

/* If we are exploring the execution of the program, 
 * write the current count to the file.
 * Else, force a context switch
 */
void increment_switch_count(){
	switch_count++;
	if(explore){
		fseek(file, 0, SEEK_SET);
		fprintf(file, "%d", switch_count);
	}
	else{
		if(switch_count == switch_index){
			sched_yield();
		}
	}
}

/* Locks the global lock and then starts
 * the thread. After execution, it marks the thread
 * as terminated and switches to the next thread.
 */
static
void* thread_main(void *arg)
{
    if(DEBUG) printf("%d: main\n", pthread_self());
    struct Thread_Arg thread_arg = *(struct Thread_Arg*)arg;
    free(arg);

    original_pthread_mutex_lock(&global_lock);
    void *ret_val = thread_arg.start_routine(thread_arg.arg);
    original_pthread_mutex_unlock(&global_lock);

    get_thread_node(pthread_self())->status = terminated;
    running_thread = master_id ^ pthread_self();

    return ret_val;
}

extern "C"
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg)
{
    if(DEBUG) printf("%d: create\n", pthread_self());
    initialize_original_functions();

    original_pthread_mutex_lock(&global_lock);
    running_thread = pthread_self();

    struct Thread_Arg *thread_arg = (struct Thread_Arg*)malloc(sizeof(struct Thread_Arg));
    thread_arg->start_routine = start_routine;
    thread_arg->arg = arg;

    int ret = original_pthread_create(thread, attr, thread_main, thread_arg);

	//Create a Thread_Node for the main thread (since this implementation only assumes a max of 2 threads)
    Thread_Node *main_info = (Thread_Node *) malloc(sizeof(Thread_Node));
    main_info->status = running;
    main_info->id = pthread_self();
    main_info->next = NULL;
    insert_thread_node(main_info);

	//Create a Thread_Node for the newly created thread
    Thread_Node *new_info = (Thread_Node *) malloc(sizeof(Thread_Node));
    new_info->status = running;
    new_info->id = *thread;
    new_info->next = NULL;
    insert_thread_node(new_info);
    master_id = pthread_self() ^ *thread;

    if(DEBUG) printf("inserted main: %d and child: %d\n", pthread_self(), *thread);

    increment_switch_count();

    return ret;
}

extern "C"
int pthread_join(pthread_t joinee, void **retval)
{
    if(DEBUG) printf("%d: join\n", pthread_self());
    initialize_original_functions();

    original_pthread_mutex_unlock(&global_lock);
    running_thread = joinee;
    if(DEBUG) printf("%d: CALLING ORIGINAL JOIN WITH %d\n", pthread_self(), joinee);
    //int result = original_pthread_join(joinee, retval);
    Thread_Node *joinee_node = get_thread_node(joinee);
    if(DEBUG) printf("%d: JOINEE_NODE: %p\n", pthread_self(), joinee_node);
	
	//wait for the joinee to finish
    while(joinee_node->status != terminated){
        running_thread = master_id ^ pthread_self();
    }
    if(DEBUG) printf("%d: JOIN: locking gl\n", pthread_self());
    original_pthread_mutex_lock(&global_lock);
    if(DEBUG) printf("%d: done joining\n", pthread_self());
    return 0;
}

extern "C"
int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if(DEBUG) printf("\n%d: lock %p\n", pthread_self(), mutex);
    initialize_original_functions();

    increment_switch_count();

    int return_value;

    if(DEBUG) printf("%d: ---About to check if lock is held\n", pthread_self());

    Mutex_Node *info = get_mutex_node(mutex);
	
	//Create a new Mutex_Node for the given mutex if one does not exist already
    if(!info){
        if(DEBUG) printf("%d: ---creating new Mutex_Node for %p\n", pthread_self(), mutex);
        Mutex_Node *new_info = (Mutex_Node *) malloc(sizeof(Mutex_Node));
        new_info->is_held = false;
        new_info->mutex = mutex;
        new_info->next = NULL;
        if(DEBUG) printf("%d: ---inserting into list\n", pthread_self());
        insert_mutex_node(new_info);
        info = new_info;
    }

    if(DEBUG) printf("%d: ---get_mutex_node returned: %p\n", pthread_self(), info);

	/* If the mutex is held by another thread, we switch to that
	 * thread and wait for it to finish.
	 */
    if(info->is_held){
        if(DEBUG) printf("%d: ---------lock is held\n", pthread_self());
        Thread_Node *self_node = get_thread_node(pthread_self());
        self_node->status = waiting;
        original_pthread_mutex_unlock(&global_lock);
        running_thread = info->holding_thread;

        while(running_thread != pthread_self() && info->is_held);

        original_pthread_mutex_lock(&global_lock);
        self_node->status = running;
        //running_thread = pthread_self();

        return_value = original_pthread_mutex_lock(mutex);
        info->holding_thread = pthread_self();
    }
    else{
        if(DEBUG) printf("%d: ---------lock isn't held\n", pthread_self());
        return_value = original_pthread_mutex_lock(mutex);
        info->is_held = true;
        info->holding_thread = pthread_self();
    }

    if(DEBUG) printf("%d: ---lock returning\n", pthread_self());
    //original_pthread_mutex_unlock(&global_lock);
    return return_value;
}

extern "C"
int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if(DEBUG) printf("%d: unlock %p\n", pthread_self(), mutex);
    initialize_original_functions();
    int ret;

    Mutex_Node *info = get_mutex_node(mutex);
    if(info->holding_thread == pthread_self()){
        info->is_held = false;
        
        original_pthread_mutex_unlock(mutex);
        if(DEBUG) printf("%d: original_pthread_mutex_unlock returned\n", pthread_self());
        increment_switch_count();
    }

    if(DEBUG) printf("%d: done unlocking %p\n", pthread_self(), mutex);
    return ret;
}

extern "C"
int sched_yield(void)
{
    if(DEBUG) printf("%d: sched_yield\n", pthread_self());
    Thread_Node *other_node = get_thread_node(master_id ^ pthread_self());
    if(!other_node || other_node->status==terminated || other_node->status==waiting){
        return 0;
    }
    original_pthread_mutex_unlock(&global_lock);
    running_thread = master_id ^ pthread_self();
    //sleep(1);
    while(running_thread != pthread_self());
    original_pthread_mutex_lock(&global_lock);

    return 0;
}

static
void initialize_original_functions()
{
    if(DEBUG) printf("%d: init\n", pthread_self());
    //static bool initialized = false;
    if (!initialized) {
        initialized = true;

        original_pthread_create =
            (int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*))dlsym(RTLD_NEXT, "pthread_create");
        original_pthread_join = 
            (int (*)(pthread_t, void**))dlsym(RTLD_NEXT, "pthread_join");
        original_pthread_mutex_lock =
            (int (*)(pthread_mutex_t*))dlsym(RTLD_NEXT, "pthread_mutex_lock");
        original_pthread_mutex_unlock =
            (int (*)(pthread_mutex_t*))dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    }

    if(!gl_is_init){
        gl_is_init = true;
        pthread_mutex_init(&global_lock, NULL);
        thread_head = (Thread_Node*) malloc(sizeof(Thread_Node));
        thread_head->id = 1337331;
        thread_head->next = NULL;
        mutex_head = (Mutex_Node*) malloc(sizeof(Mutex_Node));
        mutex_head->mutex = NULL;
        mutex_head->next = NULL;

        file = fopen("chess_sequence", "r");
        if(file==NULL){
            explore = true;
            file = fopen("chess_sequence", "w");
        }
        else{
            explore = false;
            int err = fscanf(file, "%d", &switch_index);
            if(!err){
                printf("Couldn't read chess sequence file.\n");
            }
            fclose(file);
        }
    }
}
