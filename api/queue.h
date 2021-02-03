#include <stdint.h>

typedef struct q_node{
   int   data;
   void* addr;
   struct q_node* next;
} q_node;

typedef struct {
   q_node *head;
   q_node *tail;
   int size;
} queue;

queue* init_queue();
void enqueue(queue* q, int data, void* addr);
int dequeue(queue* q);
int get_size(queue* q);
void print_queue(queue* q);
queue* free_queue(queue* q);