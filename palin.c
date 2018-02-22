/*
 * $Author: o1-mccune $
 * $Date: 2017/09/28 04:00:24 $
 * $Revision: 1.7 $
 * $Log: palin.c,v $
 * Revision 1.7  2017/09/28 04:00:24  o1-mccune
 * Fixed 2nd parameter of output string to string index instead of child id.
 *
 * Revision 1.6  2017/09/28 03:39:42  o1-mccune
 * Added stderr message specifying what signal the process received.
 *
 * Revision 1.5  2017/09/28 03:25:56  o1-mccune
 * Added rcs keywords and updated maxWrites back to 5.
 *
 *
 */


#include <sys/ipc.h>
#include <sys/types.h>
#include "master.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/shm.h>
#include <time.h>
#include <inttypes.h>

static shared_memory_list_t *sharedMemory;
static int sharedMemoryId;
static int petersonSharedMemoryId;
static peterson_t *peterson;
static key_t sharedMemoryKey;
static int j;
static int id;
static int listNum;
static int maxWrites = 5;
static FILE *palin;
static FILE *noPalin;

static void printOptions(){
  fprintf(stdout, "Command Help\n");
  fprintf(stdout, "\t-h: Prints Command Usage\n");
  fprintf(stdout, "\t-i: ID of the shared memory list.\n");
  fprintf(stdout, "\t-p: ID of the shared synchronization memory structure.\n");
  fprintf(stdout, "\t-l: ID of the string to parse in the shared memory list.\n");
  fprintf(stdout, "\t-c: ID of this child process.\n");
}

static void parseOptions(int argc, char *argv[]){
  int c;
  while ((c = getopt (argc, argv, "hi:p:l:c:")) != -1){
    switch (c){
      case 'h':
       // printOptions();
       // cleanUp(1);
      case 'i':
        sharedMemoryId = atoi(optarg);
        break;
      case 'p':
        petersonSharedMemoryId = atoi(optarg);
        break;
      case 'l':
        listNum = atoi(optarg);
        break;
      case 'c':
        id = atoi(optarg);
        break;
      case '?':
        if(isprint (optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        default:
          abort();
    }
  }
}


time_t getTimeInNanosecond(){
  time_t timeInNano;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  timeInNano = (uint64_t) ts.tv_sec * 1000000000L + (uint64_t) ts.tv_nsec;
  return timeInNano;
}

int attachMemory(){
  if((sharedMemory = shmat(sharedMemoryId, NULL, 0)) == (void *)-1) perror("Palin: Failed to attach Shared List Memory");
  if((peterson = shmat(petersonSharedMemoryId, NULL, 0)) == (void *)-1) perror("Palin: Failed to attach Peterson Shared Memory");
  return 0;
}

int detachMemory(){
  if(sharedMemory){
    if(shmdt(sharedMemory) == -1){
      perror("Palin: Failed to detach Shared List Memory");
      return -1;
    }
  }
  if(peterson){
    if(shmdt(peterson) == -1){
      perror("Palin: Failed to detach Shared List Memory"); 
      return -1;
    }
  }
  return 0;
}


int isPalindrome(char palindrome[]){
  size_t length = strlen(palindrome);
  int centerOffset = 0;
	
  if(length == 0) return -1; //empty string
  else if(length % 2 == 0) centerOffset = 0;  //even string length
  else centerOffset = 1;  //odd string length
  int iterator;
  for(iterator = 0; iterator < (length-centerOffset)/2; iterator++){
    if((palindrome[iterator] >= 65 && palindrome[iterator] <= 90) || (palindrome[iterator] >= 97 && palindrome[iterator] <= 122)){  //char is alpha
      int difference = abs(palindrome[length - 1 - iterator] - palindrome[iterator]);
      if(difference == 0 || difference == 32) ;  
      else return -1; //char is not the same, return failure
    }
    else{  //char is other
      if(palindrome[iterator] != palindrome[length - 1 - iterator]) return -1; //chars do not match; return failure
    }
  } 	
  return 1;
}

static void cleanUp(int status){
  if(detachMemory() == -1) perror("Palin: Failed to detach shared memory");
  if(palin) fclose(palin);
  if(noPalin) fclose(noPalin);
  exit(status);
}

static void signalHandler(int signal){
  if(signal == 14) fprintf(stderr, "Palin: PID: %7d : Exiting due to SIGALRM\n", getpid());
  if(signal == 2) fprintf(stderr, "Palin: PID: %7d : Exiting due to SIGINT\n", getpid());
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

int main(int argc, char **argv){


  parseOptions(argc, argv);
  if(createInterruptWatcher() == -1) perror("Failed to setup SIGINT watcher");
  if(createAlarmWatcher() == -1) perror("Failed to setup SIGALRM watcher");
  if(attachMemory() == -1) perror("Palin: Failed to attach shared memory");
  if((palin = fopen("./palin.out", "a")) == NULL){
    perror("Palin: Failed to open palin.out");
    exit(-1);
  }
  if((noPalin = fopen("./nopalin.out", "a")) == NULL){
    perror("Palin: Failed to open nopalin.out");
    exit(-2);
  } 
  int sleepTime;
  int i;
  for(i = 0; i < maxWrites; i++){
    do{
      fprintf(stderr, "PID: %7d \tID: %2d \tREQUESTING CRITICAL SECTION @ %lu\n", getpid(), id, getTimeInNanosecond());
      peterson->flag[id-1] = WANT_IN;
      j = peterson->turn;
      while(j != id-1){
        j = (peterson->flag[j] != IDLE) ? peterson->turn : (j + 1) % (peterson->participants + 1);
      }
      peterson->flag[id-1] = IN_CS;

      for(j = 0; j < peterson->participants; j++)
        if((j != id-1) && (peterson->flag[j] == IN_CS)) break;
    }while((j < peterson->participants) || (peterson->turn != id-1 && peterson->flag[peterson->turn] != IDLE));
    peterson->turn = id-1;
    
    //critical section
    fprintf(stderr, "PID: %7d \tID: %2d \t    ENTERS CRITICAL SECTION @ %lu\n", getpid(), id, getTimeInNanosecond());
    sleepTime = rand() % 3;
    sleep(sleepTime);
    if(isPalindrome(sharedMemory->list[listNum]) == 1){
      fprintf(palin, "%d  %d  %s\n", getpid(), listNum, sharedMemory->list[listNum]);
      fflush(palin);
    }
    else{
      fprintf(noPalin, "%d  %d  %s\n", getpid(), listNum, sharedMemory->list[listNum]);
      fflush(noPalin);
    }
    sleepTime = rand() % 3;    
    sleep(sleepTime);
    fprintf(stderr, "PID: %7d \tID: %2d \t    LEAVES CRITICAL SECTION @ %lu\n", getpid(), id, getTimeInNanosecond());
    //exit section
    j = (peterson->turn + 1) % (peterson->participants + 1);
    while(peterson->flag[j] == IDLE)
      j = (j + 1) % (peterson->participants + 1);

    peterson->turn = j;
    peterson->flag[id-1] = IDLE;
  }
  cleanUp(2);
  return 0;
}
