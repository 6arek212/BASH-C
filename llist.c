#include <stdlib.h>
#include "llist.h"

void add(List *list, void *data)
{
	if (!list)
	{
		return;
	}

	Node *node = (Node *)malloc(sizeof(Node));
	node->next = NULL;
	node->prev = list->tail;
	node->data = data;
	list->size++;

	if (!list->head)
	{
		list->head = list->tail = node;
		return;
	}

	list->tail->next = node;
	list->tail = node;
}

void *remove1(List *list, int index)
{
	if (!list || list->size == 0)
	{
		return NULL;
	}
	Node *p = list->head;
	int cnt = 0;

	while (cnt < index)
	{
		p = p->next;
	}

	if (index == 0)
	{
		list->head = list->head->next;
	}
	else
	{
		p->prev->next = p->next;
	}

	if (index == list->size - 1)
	{
		list->tail = p->next;
	}

	list->size--;
	void *data = p->data;
	free(p);
	return data;
}

void *get(List *list, int index)
{
	if (!list || !list->head)
	{
		return NULL;
	}

	Node *p = list->head;
	int cnt = 0;

	while (cnt < index)
	{
		p = p->next;
		cnt++;
	}

	return p->data;
}
