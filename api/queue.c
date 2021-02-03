#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

queue* init_queue(){
    queue* q;

    if(q != NULL)
        printf("Queue already initalized!");
    else
    {
        q = (queue*) malloc(sizeof(queue));
        q->head = NULL;
        q->tail = NULL;
        q->size = 0;
    }
    
    return q;
}// init_queue

void enqueue(queue* q, int data, void* addr){
    
    // Create new node
    q_node* new_node = (q_node*) malloc(sizeof(q_node));
    new_node->data = data;
    new_node->addr = addr;
    new_node->next = NULL;

    //Add node to the queue
    if(q->tail == NULL)
        q->head = (struct q_node*) new_node;
    else
        q->tail->next = new_node;

    q->tail = new_node;
    q->size++;
}// enqueue

int dequeue(queue* q){    

    q_node *rm_node = q->head;
    int ret_val = rm_node->data;

    q->head = q->head->next;
    free(rm_node);

    q->size-=1;
    return ret_val;

}// dequeue

int get_size(queue* q){
    return q->size;
}// get_size

void print_queue(queue* q){
    q_node *print_node = q->head;
    printf("Q: ");
    while(print_node != NULL){
        printf("[%d, 0x%X]; ", print_node->data, print_node->addr);
        print_node = print_node->next;
    }
    printf("\n");
}// print_queue

queue* free_queue(queue* q){
    q_node *free_node = q->head;
    while(free_node != NULL){
        q_node *current_node = free_node;
        free_node = free_node->next;
        free(current_node);
    }
    free(q);
    return NULL;
}// free_queue
