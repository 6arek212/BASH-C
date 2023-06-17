typedef struct Node
{
	void *data;
	struct Node *next;
	struct Node *prev;
} Node;



typedef struct List
{
	int size;
	struct Node *head;
	struct Node *tail;
} List;


void add(List *list, void *data);
void *remove1(List *list, int index);
void *get(List *list, int index);