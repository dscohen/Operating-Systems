#include <stdio.h>
#include <stdlib.h>

#include "hardware.h"
#include "drivers.h"
#include "kernel.h"


/* You may use the definitions below, if you are so inclined, to
   define the process table entries. Feel free to use your own
   definitions, though. */

typedef enum { RUNNING, READY, BLOCKED , UNINITIALIZED } PROCESS_STATE;


typedef struct process_table_entry {
  PROCESS_STATE state;
  int total_CPU_time_used;
} PROCESS_TABLE_ENTRY;

PROCESS_TABLE_ENTRY process_table[MAX_NUMBER_OF_PROCESSES];



/* Since you will have a ready queue as well as a queue for each semaphore,
   where each queue contains PIDs, here are two structure definitions you can
   use, if you like, to implement a single queue.  
   In this case, a queue is a linked list with a head pointer 
   and a tail pointer. */


typedef struct PID_queue_elt {
  struct PID_queue_elt *next;
  PID_type pid;
} PID_QUEUE_ELT;


typedef struct {
  PID_QUEUE_ELT *head;
  PID_QUEUE_ELT *tail;
} PID_QUEUE;

PID_QUEUE ready_list;
int ioreq = 0;


/* This constant defines the number of semaphores that your code should
   support */

#define NUMBER_OF_SEMAPHORES 16
#define INITIAL_SEMAPHORE_VALUE 1
PID_QUEUE semaphore_table[NUMBER_OF_SEMAPHORES];
//initialize values
int semaphore_values[NUMBER_OF_SEMAPHORES] = {[0 ... (NUMBER_OF_SEMAPHORES - 1)] = INITIAL_SEMAPHORE_VALUE};
/* This is the initial integer value for each semaphore (remember that
   each semaphore should have a queue associated with it, too). */



/* A quantum is 40 ms */

#define QUANTUM 40


/* This variable can be used to store the current value of the clock
   when a process starts its quantum. Later on, when an interrupt
   (of any kind) occurs, if the difference between the current time
   and the quantum start time is greater or equal to QUANTUM (40), 
   then the current process has used up its quantum. */

int current_quantum_start_time;



void fork_process(PID_type, int);
void terminate_process(int);
void change_semaphore(PID_type, int, int);

int check_done(PROCESS_STATE);
void add_tail(PID_QUEUE *queue, PID_type);
void run_another();

void handle_trap(void){
  switch(R1) {
    case 0:
      printf("%d : %d process requests disk read\n", clock, current_pid);
      ioreq++;
      disk_read_req(current_pid, R2);
      process_table[current_pid].state = BLOCKED;
      run_another();
      break;
    case 1:
      printf("%d : %d process requests disk write\n", clock, current_pid);
      disk_write_req(current_pid);
      break;
    case 2:
      printf("%d : %d process requests keyboard read\n", clock, current_pid);
      ioreq++;
      keyboard_read_req(current_pid);
      process_table[current_pid].state = BLOCKED;
      run_another();
      break;
    case 3:
      //write code
      printf("%d : %d process forks to create %d process\n", clock, current_pid, R2);
      fork_process(current_pid, R2);
      break;
    case 4:
      //write code
      printf("%d : %d process terminates with %d total CPU time\n", clock, current_pid, process_table[current_pid].total_CPU_time_used + (clock - current_quantum_start_time));
      terminate_process(current_pid);
      break;
    case 5:
      //write code
      change_semaphore(current_pid, R2, R3);
      break;
    default :
      break;
  }
  return;
}
//Handle Trap functions********************************************************************
//updates R2 value in process table, adds R2 to queue
void fork_process(PID_type current_pid, int new_pid){
  process_table[new_pid].state = READY;
  PID_QUEUE *r = &ready_list;
  add_tail(r,new_pid);
  return;
}

void terminate_process(PID_type pid){
  process_table[pid].state = UNINITIALIZED;
  run_another();
  return;
}

void change_semaphore(PID_type pid, int sem_index, int operand){
  //already locked, and tries to lock
  if (semaphore_values[sem_index] == 0 && (operand == 0)) {
    printf("%d : %d process requests DOWN on semaphore %d\n", clock, current_pid, sem_index);
    process_table[pid].state = BLOCKED;
    PID_QUEUE *r = &semaphore_table[sem_index];
    add_tail(r,pid);
    run_another();
    return;
  }
  //locked, now unlocking
  if (semaphore_values[sem_index] == 0 && (operand == 1)) {
    printf("%d : %d process requests UP on semaphore %d\n", clock, current_pid, sem_index);
    semaphore_values[sem_index]++;
    //check to see if any code is waiting
    if (semaphore_table[sem_index].head == NULL)
      return;
    //unblock head of waiting and add it to ready list
    PID_type unblock = semaphore_table[sem_index].head->pid;
    process_table[unblock].state = READY;
    semaphore_table[sem_index].head =  semaphore_table[sem_index].head->next;
    PID_QUEUE *r = &ready_list;
    add_tail(r, unblock);
    return;
  }
  //unlock -> lock
  if (semaphore_values[sem_index] == 1 && (operand == 0)) {
    printf("%d : %d process requests DOWN on semaphore %d\n", clock, current_pid,sem_index);
    semaphore_values[sem_index]--;
  }
  else {
    //unlocked, and unlocks, do nothing
    printf("%d : %d process requests UP on semaphore %d\n", clock, current_pid, sem_index);
  }
  //****
  return;
}





void handle_clock(){
  if (clock - current_quantum_start_time >= QUANTUM){
    //set new pid to waiting head of queue, move head to following, update tail
    if (process_table[current_pid].state == RUNNING) {
      process_table[current_pid].state = READY;
      PID_QUEUE *r = &ready_list;
      add_tail(r, current_pid);
      run_another();
    }
  }
  return;
}

void handle_disk(){
  printf("%d : DISK_INTERRUPT for process %d\n", clock, R1);
  ioreq--;
  int empty = check_done(BLOCKED);
  process_table[R1].state = READY;
  PID_QUEUE *r = &ready_list;
  add_tail(r, R1);
  if (empty == 1) {
    run_another();
  }
}

void handle_keyboard(){
  printf("%d : KEYBOARD_INTERRUPT for process %d\n", clock, R1);
  ioreq--;
  int empty = check_done(BLOCKED);
  process_table[R1].state = READY;
  PID_QUEUE *r = &ready_list;
  add_tail(r, R1);
  if (empty == 1) {
    run_another();
  }
}



//HELPER FUNCTIONS
//adds pid to the tail queue as ready, updates process table

int check_done(PROCESS_STATE state){
  int i;
  if (state == BLOCKED) {
    for(i = 0; i < MAX_NUMBER_OF_PROCESSES; i++) {
      if (process_table[i].state == READY || process_table[i].state == RUNNING) {
        return 0;
      }
    }
  }
  else {
    for(i = 0; i < MAX_NUMBER_OF_PROCESSES; i++) {
      if (process_table[i].state != state) {
        return 0;
      }
    }
  }
  return 1;
}


void run_another(){
  process_table[current_pid].total_CPU_time_used += (clock - current_quantum_start_time);
  /*printf("Time pid %d has run: %d\n", current_pid, process_table[current_pid].total_CPU_time_used);*/
  current_quantum_start_time = clock;
  if (ready_list.head != NULL) {
    printf("%d : running process %d\n",clock, ready_list.head->pid);
    current_pid = ready_list.head->pid;
    process_table[current_pid].state = RUNNING;
    PID_QUEUE_ELT * chop_block = ready_list.head;
    ready_list.head = ready_list.head->next;
    free(chop_block);
  } else {
    current_pid = -1;
    if (check_done(BLOCKED) && (ioreq == 0) && current_pid == -1) {
      printf("DEADLOCK\n");
      exit(0);
    }
    if (check_done(UNINITIALIZED) == 0) {
      printf("%d : CPU idle\n", clock);
    } else {
      printf("DONE\n");
      exit(0);
    }
  }
}



//does not set state, adds pid to queue
void add_tail(PID_QUEUE *queue, PID_type pids) {

  PID_QUEUE_ELT * value = malloc(sizeof(PID_QUEUE_ELT));
  value->next = NULL;
  value->pid = pids;
  //check to see if empty
  if (queue->head == NULL) {
    queue->head = value;
    queue->tail = value;
    return;
  }
  queue->tail->next = value;
  queue->tail = value;
  return;
}

/* This procedure is automatically called when the 
   (simulated) machine boots up */

void initialize_kernel()
{
  int i;
  for (i = 1; i < MAX_NUMBER_OF_PROCESSES; i++) {
    process_table[i].state = UNINITIALIZED;
  }
  process_table[0].state = RUNNING;
  current_quantum_start_time = 0;
  ready_list.head = NULL;
  ready_list.tail = NULL;

  INTERRUPT_TABLE[TRAP] = handle_trap;
  INTERRUPT_TABLE[CLOCK_INTERRUPT] = handle_clock;
  INTERRUPT_TABLE[DISK_INTERRUPT] = handle_disk;
  INTERRUPT_TABLE[KEYBOARD_INTERRUPT] = handle_keyboard;
}

// Put any initialization code you want here.
// Remember, the process 0 will automatically be
// executed after initialization (and current_pid
// will automatically be set to 0), 
// so the your process table should initially reflect 
// that fact.

// Don't forget to populate the interrupt table
// (see hardware.h) with the interrupt handlers
// that you are writing in this file.

// Also, be sure to initialize the semaphores as well
// as the current_quantum_start_time variable.


