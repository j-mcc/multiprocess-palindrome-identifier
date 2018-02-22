/* $Author: o1-mccune $
 * $Date: 2017/09/28 03:31:15 $
 * $Revision: 1.8 $
 * $Log: master.c,v $
 * Revision 1.8  2017/09/28 03:31:15  o1-mccune
 * Updated maxChildren to LIST_SIZE for final release.
 *
 * Revision 1.7  2017/09/28 03:23:29  o1-mccune
 * Adding rcs keywords.
 *
 */


#include "master.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <inttypes.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/errno.h>


static FILE *file;
static char *filePath;


static int maxNumberChildren = LIST_SIZE;
static int aliveChildren;

static int maxProcessTime = 60;
static shared_memory_list_t *sharedMemory;
static peterson_t *peterson;
static int sharedMemoryId;
static int petersonSharedMemoryId;
static key_t sharedMemoryKey;
static pid_t *children;

static int addProcess(int index, pid_t processId){
  if(!processId) return -1; //process ID can't be null
  children[index] = processId;
  return 0;
}


/*
 * Finds a childpid in the child pid list and returns its index
 */
static int findId(pid_t childpid){
  int i;
  for(i = 0; i < SIMULTEANEOUS_PROCESSES; i++){
    if(children[i] == childpid) return i;
  }
  return -1;
}

time_t getTimeInNanosecond(){
  time_t timeInNano;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  timeInNano = (uint64_t) ts.tv_sec * 1000000000L + (uint64_t) ts.tv_nsec;
  return timeInNano;
}

static void printOptions(){
  fprintf(stdout, "Command Help\n");
  fprintf(stdout, "\t-h: Prints Command Usage\n");
  fprintf(stdout, "\t-t: Input number of seconds before the main process terminates. Default is 60 seconds.\n");
}

static void parseOptions(int argc, char *argv[]){
  int c;
  while ((c = getopt (argc, argv, "ht:f:")) != -1){
    switch (c){
      case 'h':
        printOptions();
        abort();
      case 't':
        maxProcessTime = atoi(optarg);
        break;
      case 'f':
	filePath = malloc(strlen(optarg) + 1);
        memcpy(filePath, optarg, strlen(optarg));
        break;
      case '?':
        if(optopt == 't'){
          maxProcessTime = 60;
	  break;
	}
	else if(isprint (optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        default:
	  abort();
    }
  }
}


/*
 * Initializes the shared memory list
 */
static int initializeSharedMemory(){
  if((sharedMemoryKey = ftok("./master", 1)) == -1) return -1;
  fprintf(stderr, "Master: Shared Memory Key: %d\n", sharedMemoryKey);
  if((sharedMemoryId = shmget(sharedMemoryKey, sizeof(shared_memory_list_t), IPC_CREAT | 0644)) == -1){
    perror("Master: Failed to get shared memory");
    return -1;
  }
  fprintf(stderr, "Master: Shared Memory ID: %d\n", sharedMemoryId);
  if((sharedMemory = shmat(sharedMemoryId, NULL, 0)) == (void *)-1) return -1;
  return sharedMemoryId;
}


/*
 * Initializes the shared memory for multiprocess synchronization
 */
static int initializePeterson(){
  if((sharedMemoryKey = ftok("./master", 2)) == -1){
    perror("Master: Failed to generate key for Peterson Shared Memory");
    return -1;
  }
  fprintf(stderr, "Master: Peterson Shared Memory Key: %d\n", sharedMemoryKey);
  if((petersonSharedMemoryId = shmget(sharedMemoryKey, sizeof(peterson_t), IPC_CREAT | 0644)) == -1){
    perror("Master: Failed to get Peterson Shared Memory");
    return -1;
  }
  if((peterson = shmat(petersonSharedMemoryId, NULL, 0)) == (void *)-1){
    perror("Master: Failed to attach Peterson Shared Memory");
    return -1;
  }
  fprintf(stderr, "Master: Peterson Shared Memory ID: %d\n", petersonSharedMemoryId);
  return petersonSharedMemoryId;
}

/*
 * Removes both the shared memory list and the multiprocesses synchronization memory
 */
static int removeSharedMemory(){
  if(shmdt(sharedMemory) == -1) perror("Failed to detach shared memory");

  if(shmctl(sharedMemoryId, IPC_RMID, NULL) == -1){
    perror("Master: Failed to Remove Shared List From Memory");
    return -1;
  }
  if(shmctl(petersonSharedMemoryId, IPC_RMID, NULL) == -1){
    perror("Master: Failed to Remove Peterson Shared Memory");
    return -1;
  }
  return 0;
}


/*
 * Handles the sending of signals to remaining processes if a ALARM or INTERRUPT signal arrives. Also cleans up any shared memory.
 */
static void cleanUp(int signal){
  int i;
  for(i = 0; i < SIMULTEANEOUS_PROCESSES; i++){
    if(children[i] > 0){
      if(signal == 2) fprintf(stderr, "Parent sent SIGINT to Child %d \t @ Time: %ld \n", children[i], getTimeInNanosecond());
      else if(signal == 14)fprintf(stderr, "Parent sent SIGALRM to Child %d \t @ Time: %ld \n", children[i], getTimeInNanosecond());
      kill(children[i], signal);
    }
  }
  if(removeSharedMemory() == -1){
    perror("Failed to remove Shared Memory");
    return;
  }
  fprintf(stderr, "Deleted Shared Memory ID: %d\n", sharedMemoryId);
}


/*
 * Fills the shared memory list with strings from a file.
 */
static void fillSharedMemory(){
  int i; 
  sharedMemory->id = 1;
  for(i = 0; i < LIST_SIZE; i++){
    char c;
    int j;
    for(j = 0; j < LIST_ITEM_SIZE - 1; j++){
      c = fgetc(file);
      if(c == EOF) break;
      else if(c == '\n') break;
      else sharedMemory->list[i][j] = c;
    }
    sharedMemory->list[i][j+1] = '\0';
    if(c == EOF) return;
  }
}


/*
 * Sets initial multiprocess synchronization variables.
 */
static void fillPetersonMemory(){
  peterson->turn = 0;
  peterson->participants = SIMULTEANEOUS_PROCESSES - 1;
}

static void signalHandler(int signal){
  cleanUp(signal);
  abort();
}

static int createAlarmWatcher(){
  struct sigaction action;
  action.sa_handler = signalHandler;
  action.sa_flags = 0;
  return (sigemptyset(&action.sa_mask) || sigaction(SIGALRM, &action, NULL));
}

static int createInterruptWatcher(){
  struct sigaction action;
  action.sa_handler = signalHandler;
  action.sa_flags = 0;
  return (sigemptyset(&action.sa_mask) || sigaction(SIGINT, &action, NULL));
}

/*
 * Transform an integer into a string
 */
static char *itoa(int num){
  char *asString = malloc(sizeof(char)*16);
  snprintf(asString, sizeof(char)*16, "%d", num);
  return asString;
}

int main(int argc, char **argv){
  parseOptions(argc, argv);

  children = malloc(SIMULTEANEOUS_PROCESSES * sizeof(pid_t));
  int childpid, i;
  
  if(createInterruptWatcher() == -1) perror("Failed to setup SIGINT watcher");
  if(createAlarmWatcher() == -1) perror("Failed to setup SIGALRM watcher");
  if(initializeSharedMemory() == -1) perror("Failed to setup Shared Memory");
  if(initializePeterson() == -1) perror("Failed to initialize Peterson Shared Memory");
  if((file = fopen(filePath, "r")) == NULL){
    perror("Failed to open file");
    if(removeSharedMemory() == -1) perror("Master: Failed to remove shared memory");
    abort();
  }
  fillSharedMemory();
  fillPetersonMemory();
  if(fclose(file) == EOF) perror("Failed to close file");


  alarm(maxProcessTime);

  /*
 * Parent spawns off a number of children to do its work.
 */
  for(i = 1; i <= SIMULTEANEOUS_PROCESSES; i++){
    childpid = fork();
    if(childpid){ //master code
      addProcess(i-1, childpid);
      aliveChildren++;
      continue;
    }else if(childpid == 0){  //child code
      execl("./palin", "./palin", "-l", itoa(i-1), " ", "-c", itoa(i), " ", "-i", itoa(sharedMemoryId), " ", "-p", itoa(petersonSharedMemoryId), NULL);
      return 0;
    }else{
      perror("Failed to fork");
      cleanUp(2);
    }
  }

  /*
 * This parent is still in its prime. Continually keep its children population healthy but replacing dead children.
 */
  while(i <= maxNumberChildren){
    if((childpid = waitpid(-1, NULL, 0)) != -1){
      aliveChildren--;
      int id;
      if((id = findId(childpid)) == -1) fprintf(stderr, "Master: Could not find %d in Children PID List\n", childpid);
      fprintf(stderr, "Master: child %d:%d has died\n", id+1, childpid);
      pid_t newChildpid = fork();
      if(newChildpid){  //parent code
        aliveChildren++;
        children[id] = newChildpid;
      }
      else if(newChildpid == 0){  //child code
        execl("./palin", "./palin", "-l", itoa(i - 1), " ", "-c", itoa(id+1), " ", "-i", itoa(sharedMemoryId)," ", "-p", itoa(petersonSharedMemoryId), NULL);
        return 0;
      }
      else{
        perror("Failed to fork");
        cleanUp(2);
      }
      i++;
    }
    else fprintf(stderr, "Master: child pid = %d\n", childpid);
  }

  /*
 * This parent has become barren. Wait for all remaining children to die, then kill itself.
 */
  while((childpid = waitpid(-1, NULL, 0) != -1 && errno != ECHILD)){
    aliveChildren--;
    int id;
    if((id = findId(childpid)) != -1){
      fprintf(stderr, "Master: Child %d : %d has died.\n", id+1, childpid);
      children[id] = -1;
    }
  }

  if(removeSharedMemory() == -1){
    perror("Failed to remove Shared Memory");
    return;
  }
  fprintf(stderr, "Master: Terminating\n");
  return 0;
}
