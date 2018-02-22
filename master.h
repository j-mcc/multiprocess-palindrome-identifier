/*
 * $Author: o1-mccune $
 * $Date: 2017/09/28 03:30:33 $
 * $Revision: 1.5 $
 * $Log: master.h,v $
 * Revision 1.5  2017/09/28 03:30:33  o1-mccune
 * Updated SIMULTEANEOUS_PROCESSES back to 20 for final release.
 *
 * Revision 1.4  2017/09/28 03:27:44  o1-mccune
 * added rcs keywords
 *
 */


#ifndef MASTER_H
#define MASTER_H

#define LIST_SIZE 50
#define LIST_ITEM_SIZE 256
#define SIMULTEANEOUS_PROCESSES 20

#include <time.h>

typedef struct{
  int id;
  char list[LIST_SIZE][LIST_ITEM_SIZE];
} shared_memory_list_t;

typedef enum{
  IDLE,
  WANT_IN,
  IN_CS
}state_t;

typedef struct{
  int participants;
  int turn;
  state_t flag[SIMULTEANEOUS_PROCESSES];
} peterson_t;


#endif
