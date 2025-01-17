#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int time_slot;
static int num_cpus;
static int done = 0;
pthread_mutex_t cpu_lock;
int process_counter = 0;

#ifdef MM_PAGING
static int memramsz;
static int memswpsz[PAGING_MAX_MMSWP];

struct mmpaging_ld_args {
    struct memphy_struct *mram;
    struct memphy_struct **mswp;
    struct memphy_struct *active_mswp;
    struct timer_id_t *timer_id;
};
#endif

static struct ld_args {
    char **path;
    unsigned long *start_time;
#ifdef MLQ_SCHED
    unsigned long *prio;
#endif
} ld_processes;
int num_processes;

struct cpu_args {
    struct timer_id_t *timer_id;
    int id;
};

// Heap management structures
typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
} heap_block_t;

static heap_block_t *heap_head = NULL;
static size_t total_heap_size = 0;
static size_t free_space = 0;
static const char *heap_stats_file = "output/heap_stats.txt";

void init_heap(size_t size) {
    heap_head = (heap_block_t *)malloc(size);
    if (!heap_head) {
        perror("Failed to initialize heap");
        exit(1);
    }
    heap_head->size = size - sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = NULL;
    total_heap_size = size;
    free_space = heap_head->size;
}

void *allocate_memory(size_t size) {
    heap_block_t *current = heap_head;

    while (current) {
        if (current->free && current->size >= size) {
            if (current->size > size + sizeof(heap_block_t)) {
                heap_block_t *new_block = (heap_block_t *)((char *)current + sizeof(heap_block_t) + size);
                new_block->size = current->size - size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = current->next;
                current->size = size;
                current->next = new_block;
            }
            current->free = 0;
            free_space -= size + sizeof(heap_block_t);
            return (char *)current + sizeof(heap_block_t);
        }
        current = current->next;
    }
    return NULL;
}

void free_memory(void *ptr) {
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((char *)ptr - sizeof(heap_block_t));
    block->free = 1;
    free_space += block->size + sizeof(heap_block_t);

    heap_block_t *current = heap_head;
    while (current && current->next) {
        if (current->free && current->next->free) {
            current->size += current->next->size + sizeof(heap_block_t);
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

void save_heap_stats() {
    FILE *file = fopen(heap_stats_file, "w");
    if (!file) {
        perror("Failed to open heap stats file");
        return;
    }

    fprintf(file, "Total heap size: %zu\n", total_heap_size);
    fprintf(file, "Free space: %zu\n", free_space);

    size_t fragmented_blocks = 0;
    heap_block_t *current = heap_head;
    while (current) {
        if (current->free) fragmented_blocks++;
        current = current->next;
    }
    fprintf(file, "Fragmented blocks: %zu\n", fragmented_blocks);

    fclose(file);
}

static void *cpu_routine(void *args) {
	int id = ((struct cpu_args *)args)->id;
	struct timer_id_t *timer_id = ((struct cpu_args *)args)->timer_id;

	
	/* Check for new process in ready queue */
	int time_left = 0;
	struct pcb_t *proc = NULL;
	while (1)
	{
		//printf("%d %d \n", time_slot, time_left);
		//if(proc!=NULL)
		//printf("Proc->pc, proc->code->size and proc->priority are: %d %d %d\n", proc->pc, proc->code->size, proc->priority);
		/* Check the status of current process */
		if (proc == NULL)
		{
			/* No process is running, the we load new process from
			 * ready queue */
			proc = get_mlq_proc();
			if(proc == NULL && process_counter >= num_processes) done = 1;
			else if (proc == NULL)
			{
				next_slot(timer_id);
				continue; /* First load failed. skip dummy load */
			}
		}
		else if (proc->pc == proc->code->size)
		{
			/* The porcess has finish it job */
			printf("\tCPU %d: Processed %2d has finished\n", id, proc->pid);
			pthread_mutex_lock(&cpu_lock);
			process_counter++;
			pthread_mutex_unlock(&cpu_lock);
#ifdef MM_PAGING
			free_pcb_memphy(proc);
#endif
			free(proc);
			
			proc = get_mlq_proc();
			time_left = 0;
		}
		else if (time_left == 0)
		{
			/* The process has done its job in current time slot */
			printf("\tCPU %d: Put process %2d to run queue\n",
				   id, proc->pid);
			put_mlq_proc(proc);
			proc = get_mlq_proc();
		}

		/* Recheck process status after loading new process */
		if (proc == NULL && done)
		{
			/* No process to run, exit */
			printf("\tCPU %d stopped\n", id);
			break;
		}
		else if (proc == NULL)
		{
			/* There may be new processes to run in
			 * next time slots, just skip current slot */
			next_slot(timer_id);
			continue;
		}
		else if (time_left == 0)
		{
			printf("\tCPU %d: Dispatched process %2d\n",
				   id, proc->pid);
			time_left = time_slot;
		}

		/* Run current process */
		run(proc);
		time_left--;
		next_slot(timer_id);
	}
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void *ld_routine(void *args) {
#ifdef MM_PAGING
	struct memphy_struct *mram = ((struct mmpaging_ld_args *)args)->mram;
	struct memphy_struct **mswp = ((struct mmpaging_ld_args *)args)->mswp;
	struct memphy_struct *active_mswp = ((struct mmpaging_ld_args *)args)->active_mswp;
	struct timer_id_t *timer_id = ((struct mmpaging_ld_args *)args)->timer_id;
#else
	struct timer_id_t *timer_id = (struct timer_id_t *)args;
#endif
	int i = 0;
	printf("ld_routine\n");
	while (i < num_processes)
	{

		struct pcb_t *proc = load(ld_processes.path[i]);
#ifdef MLQ_SCHED

		proc->prio = ld_processes.prio[i];
#endif
		while (current_time() < ld_processes.start_time[i])
		{
			next_slot(timer_id);
		}
#ifdef MM_PAGING
		proc->mm = malloc(sizeof(struct mm_struct));
		init_mm(proc->mm, proc);
		proc->mram = mram;
		proc->mswp = mswp;
		proc->active_mswp = active_mswp;
#endif
#ifdef MLQ_SCHED
		printf("\tLoaded a process at %s, PID: %d PRIO: %lu\n",
			   ld_processes.path[i], proc->pid, ld_processes.prio[i]);
#else
        printf("\tLoaded a process at %s, PID: %d PRIO: %u\n",
               ld_processes.path[i], proc->pid, proc->priority);
#endif
		put_mlq_proc(proc);
		free(ld_processes.path[i]);
		
		i++;
		// printf("i = %d\n", i);
		next_slot(timer_id);
		// printf("i = %d\n", i);
	}	
	//printf("ld_routine done\n");
	free(ld_processes.path);
	
	free(ld_processes.start_time);
	
	done = 1;
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void read_config(const char *path) {
    FILE *file;
	if ((file = fopen(path, "r")) == NULL)
	{
		printf("Cannot find configure file at %s\n", path);
		exit(1);
	}
	fscanf(file, "%d %d %d\n", &time_slot, &num_cpus, &num_processes);
	//timeslot = time_slot;
	// printf("time_slot = %d, num_cpus = %d, num_processes = %d\n", time_slot, num_cpus, num_processes);
	ld_processes.path = (char **)malloc(sizeof(char *) * num_processes);
	ld_processes.start_time = (unsigned long *)
		malloc(sizeof(unsigned long) * num_processes);
#ifdef MM_PAGING

	int sit;
    #ifdef MM_FIXED_MEMSZ
	
        /* We provide here a back compatible with legacy OS simulatiom config file
         * In which, it has no addition config line for Mema, keep only one line
         * for legacy info
         *  [time slice] [N = Number of CPU] [M = Number of Processes to be run]
         */
        memramsz = 0x100000;
        memswpsz[0] = 0x1000000;
        for (sit = 1; sit < PAGING_MAX_MMSWP; sit++)
            memswpsz[sit] = 0;
    #else
        /* Read input config of memory size: MEMRAM and upto 4 MEMSWP (mem swap)
         * Format: (size=0 result non-used memswap, must have RAM and at least 1 SWAP)
         *        MEM_RAM_SZ MEM_SWP0_SZ MEM_SWP1_SZ MEM_SWP2_SZ MEM_SWP3_SZ
         */
        fscanf(file, "%d\n", &memramsz);
        for (sit = 0; sit < PAGING_MAX_MMSWP; sit++)
            fscanf(file, "%d", &(memswpsz[sit]));

        fscanf(file, "\n"); /* Final character */
    #endif

#endif

#ifdef MLQ_SCHED
    ld_processes.prio = (unsigned long *)malloc(sizeof(unsigned long) * num_processes);
#endif

    //#ifdef MLQ_SCHED
        int i;
        for (i = 0; i < num_processes; i++)
        {
            ld_processes.path[i] = (char *)malloc(sizeof(char) * 100);
            ld_processes.path[i][0] = '\0';
            strcat(ld_processes.path[i], "input/proc/");
            char proc[100];

            #ifdef MLQ_SCHED
				//printf("Processing MLQ\n");
                fscanf(file, "%lu %s %lu", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);
				// printf("Prio: %lu\n", ld_processes.prio[i]);

            #else
                fscanf(file, "%lu %s\n", &ld_processes.start_time[i], proc);

            #endif
                strcat(ld_processes.path[i], proc);

	}
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: os [path to configure file]\n");
        return 1;
    }

    char path[100];
    path[0] = '\0';
    strcat(path, "input/");
    strcat(path, argv[1]);

    read_config(path);

    init_heap(1024 * 1024); // Initialize heap with 1 MB

    pthread_t *cpu = (pthread_t *)malloc(num_cpus * sizeof(pthread_t));
    struct cpu_args *args = (struct cpu_args *)malloc(sizeof(struct cpu_args) * num_cpus);
    pthread_t ld;

    pthread_mutex_init(&cpu_lock, NULL);
    
	/* Init timer */
	int i;
	for (i = 0; i < num_cpus; i++)
	{
		args[i].timer_id = attach_event();
		args[i].id = i;
	}
	struct timer_id_t *ld_event = attach_event();
	start_timer();

#ifdef MM_PAGING
	/* Init all MEMPHY include 1 MEMRAM and n of MEMSWP */
	int rdmflag = 1; /* By default memphy is RANDOM ACCESS MEMORY */

	struct memphy_struct mram;
	struct memphy_struct mswp[PAGING_MAX_MMSWP];
	
	/* Create MEM RAM */
	init_memphy(&mram, memramsz, rdmflag);

	/* Create all MEM SWAP */
	int sit;
	for (sit = 0; sit < PAGING_MAX_MMSWP; sit++)
		init_memphy(&mswp[sit], memswpsz[sit], rdmflag);

	/* In Paging mode, it needs passing the system mem to each PCB through loader*/
	struct mmpaging_ld_args *mm_ld_args = malloc(sizeof(struct mmpaging_ld_args));
	//printf("Init Paging mode\n");

	mm_ld_args->timer_id = ld_event;
	mm_ld_args->mram = (struct memphy_struct *)&mram;
	mm_ld_args->mswp = (struct memphy_struct **)&mswp;
	mm_ld_args->active_mswp = (struct memphy_struct *)&mswp[0];
#endif

	/* Init scheduler */
	init_scheduler();

	/* Run CPU and loader */
#ifdef MM_PAGING
	//printf("Running Paging mode\n");
	pthread_create(&ld, NULL, ld_routine, (void *)mm_ld_args);
#else
	pthread_create(&ld, NULL, ld_routine, (void *)ld_event);
#endif
	
	for (i = 0; i < num_cpus; i++)
	{
		pthread_create(&cpu[i], NULL,
					   cpu_routine, (void *)&args[i]);
	}

	/* Wait for CPU and loader finishing */
	for (i = 0; i < num_cpus; i++)
	{
		pthread_join(cpu[i], NULL);
	}
	pthread_join(ld, NULL);

	/* Stop timer */
	stop_timer();

#ifdef MM_PAGING
	pthread_mutex_destroy(&mram.lock);
	pthread_mutex_destroy(&mram.fifo_lock);
	i = 0;

	while(i < PAGING_MAX_MMSWP){
		//printf("Free mem %d\n", i);
		pthread_mutex_destroy(&mswp[i].lock);
		pthread_mutex_destroy(&mswp[i].fifo_lock);
		i++;
	}
#endif

    save_heap_stats(); // Save heap statistics to file before exit

    return 0;
}