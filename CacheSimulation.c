#include <stdio.h>
#include <stdlib.h>

#define pagesize 4096
#define cachesize (10*1024*1024)

//#define hashsize (1024*1024)
#define hashsize 4096

#define readlentency 7
#define writelentency 128
#define false 0
#define true 1

#define error -1
#define success 0

//ȫ�ֱ���(�Ƿ���������?��������⣬��ָ��һ��һ�����´�)
unsigned long long totalpage; //trace���ܹ��漰����page����
unsigned long long totaltime; //trace����ʱ��
unsigned long long hit_readpage;
unsigned long long no_hit_readpage;
unsigned long long totalreadreq;

typedef struct tagIOReq
{
	int sid;
	unsigned long long sectorid;
	unsigned long long totalbytenum;
	char RWType;
	double time;
}IOReq;

typedef struct tagNode
{
	unsigned long pageid;
	struct tagNode *prev;
	struct tagNode *next;
}Node;

typedef struct tagHashnode
{
	unsigned long id;
	Node *thepage;
	struct tagHashnode *next;
}Hashnode;

static Hashnode* hashtable[hashsize];

//����traceIO
int analysis_trace_IOreq(FILE *tracefile, IOReq *event)
{
	//	printf("begin:analysis_trace_IOre\n");
	char line[201];

	if (fgets(line, 200, tracefile) == NULL)
	{
		printf("fgets failed\n");
		return error;
	}

	if (sscanf(line, "%d,%lld,%lld,%c,%lf", &event->sid, &event->sectorid, &event->totalbytenum, &event->RWType, &event->time) != 5)
	{
		printf("Wrong number of arguments for I/O trace event type\n");
		printf("line: %s", line);
		return error;
	}

	//	printf("end:analysis_trace_IOre\n");
	return success;
}

//��ʼ��hash��
void hashtable_init()
{
	int i;
	for (i = 0; i < hashsize; i++)
	{
		hashtable[i] = NULL;
	}
}

//��ʼ��˫������
Node *List_head_init()
{
	Node *headnode = (Node *)malloc(sizeof(Node));

	if (NULL == headnode)
	{
		printf("init list failed\n");
		return NULL;
	}

	headnode->pageid = 0; //ͷ����pageid���ڵ�ǰcache��page����
	headnode->next = headnode;
	headnode->prev = headnode;
	return headnode;
}

//����hash�����Ƿ�����
Hashnode *search_hash(unsigned long pageid)//����pageid
{
	//	printf("begin:search_hash\n");
	unsigned long hash_id = pageid % hashsize;

	Hashnode* hashnode;
	hashnode = hashtable[hash_id];

	while (NULL != hashnode)
	{
		if (pageid == hashnode->id)
		{
			return hashnode;
		}
		hashnode = hashnode->next;
	}
	//	printf("end:search_hash\n");
	return NULL;
}

//pageid���д������̣�����page�Ƶ�ͷ��,totalpage+1��totaltime+readlentency
void move_page_to_head(Node *head, Node *thenode)
{
	//	printf("begin:move_page_to_head\n");
	if (head->next == thenode)
	{
		return;
	}
	thenode->next->prev = thenode->prev;
	thenode->prev->next = thenode->next;
	head->next->prev = thenode;
	thenode->next = head->next;
	thenode->prev = head;
	head->next = thenode;
	//	printf("end:move_page_to_head\n");
}

//ɾ��pagelistβ����Ӧ��hashtable�ڵ�
void delete_hashnode(unsigned long pageid)
{
	//	printf("begin:delete_hashnode\n");
	unsigned long hash_id = pageid % hashsize;

	Hashnode* hashnode;
	Hashnode* hashnodenext;
	hashnode = hashtable[hash_id];

	while (NULL != hashnode)
	{
		if (pageid == hashnode->id)
		{
			if (NULL == hashnode->next)
			{
				hashtable[hash_id] = NULL;//
				return;
			}
			hashnodenext = hashnode->next;
			hashnode->id = hashnode->next->id;
			hashnode->thepage = hashnode->next->thepage;
			hashnode->next = hashnode->next->next;
			free(hashnodenext);
			return;
		}
		hashnode = hashnode->next;
	}
	//	printf("end:delete_hashnode\n");
}

//ɾ��pagelistβ���ڵ�
void delete_thelastnode(Node *head)
{
	//	printf("begin:delete_thelastnode\n");
	Node *node = head->prev;
	node->prev->next = head;
	head->prev = node->prev;
	head->pageid--;//listͷ��������1
	free(node);
	//	printf("end:delete_thelastnode\n");
}

//���һ���½ڵ���pagelist�ײ�
Node *insert_new_list(Node *head, unsigned long id)
{
	//	printf("begin:insert_new_list\n");
	Node *new = (Node*)malloc(sizeof(Node));
	if (NULL == new)
	{
		printf("malloc new page failed\n");
		return NULL;
	}

	new->pageid = id;

	if (head == head->next)//����Ϊ��
	{
		head->next = new;
		head->prev = new;
		new->next = head;
		new->prev = head;
		head->pageid++;//������һ
		return new;
	}

	head->next->prev = new;
	new->next = head->next;
	head->next = new;
	new->prev = head;
	head->pageid++;//������һ
	return new;
}

//���һ�����hashtable
Hashnode *insert_new_hashtable(Node *node, unsigned long pageid)
{
	//	printf("begin:insert_new_hashtable\n");
	unsigned long hash_id = pageid % hashsize;

	Hashnode* hashnode = (Hashnode*)malloc(sizeof(Hashnode));
	if (NULL == hashnode)
	{
		printf("malloc new hashnode failed\n");
		return NULL;
	}
	hashnode->id = pageid;
	hashnode->next = NULL;
	hashnode->thepage = node;

	if (NULL == hashtable[hash_id])
	{
		hashtable[hash_id] = hashnode;
		return hashnode;
	}

	if (NULL == hashtable[hash_id]->next)//
	{
		hashtable[hash_id]->next = hashnode;//
		return hashnode;//
	}

	hashnode->next = hashtable[hash_id]->next;//
	hashtable[hash_id]->next = hashnode;//
	return hashnode;
}

//��pagelistͷ�����һ���µ�page��������hashtable���������
int insert_new_to_pagelist_and_hashtable(Node *head, unsigned long id)
{
	//	printf("begin:insert_new_to_pagelist_and_hashtable\n");
	Node *newnode;
	newnode = insert_new_list(head, id);
	if (NULL == newnode)
	{
		printf("insert new page to list failed\n");
		return error;
	}

	Hashnode *newhashnode;
	newhashnode = insert_new_hashtable(newnode, id);
	if (NULL == newhashnode)
	{
		printf("insert new  to hashtable failed\n");
		return error;
	}
	//	printf("end:insert_new_to_pagelist_and_hashtable\n");
	return success;
}

//pageid�����еĴ������̣������ж�cache�Ƿ����ˣ����˾ͽ�����̭����̭β������������ֱ����ӣ��ӵ�ͷ������totalpage+1��totaltime+writelentency
int page_not_hit(Node *head, unsigned long id)
{
	//	printf("begin:page_not_hit\n");
	unsigned long pagemaxnum = cachesize / pagesize;
	if (head->pageid < pagemaxnum)
	{
		//���
		if (error == insert_new_to_pagelist_and_hashtable(head, id))
			return error;
	}
	else
	{
		//����̭		
		delete_hashnode(head->prev->pageid);
		delete_thelastnode(head);//head��pagelistͷ���
		//���
		if (error == insert_new_to_pagelist_and_hashtable(head, id))
			return error;
	}

	return success;
}

int each_page_execute(char rwtype, Node* head, unsigned long pageid)
{
	//	printf("begin:each_page_executen");
	//�жϲ�������
	if ('R' == rwtype || 'r' == rwtype)
	{
		Hashnode *hashnode = search_hash(pageid);
		if (NULL != hashnode)
		{
			//����
			move_page_to_head(head, hashnode->thepage);//head��pagelistͷ���
			totalpage++;
			hit_readpage++;
			totaltime += readlentency;
		}
		else
		{
			//������
			if (error == page_not_hit(head, pageid))
				return error;
			totalpage++;
			no_hit_readpage++;
			totaltime += writelentency;
		}
	}
	else if ('W' == rwtype || 'w' == rwtype)
	{
		return success;
	}
	else
	{
		printf("rwtype is illegal\n");
		return error;
	}

	return success;
}

int each_trace_IO_execute(Node* head, IOReq *trace_io)
{
	/*	//test
	trace_io->sid = 5;
	trace_io->sectorid = 170843704;
	trace_io->totalbytenum = 28672;
	trace_io->RWType = 'R';
	trace_io->time = 10.373777;
	//test
	*/

	char rwtype = trace_io->RWType;

	if ('R' == rwtype || 'r' == rwtype)
	{
		totalreadreq++;
	}
	else if ('W' == rwtype || 'w' == rwtype)
	{
		;
	}
	else
	{
		printf("rwtype is illegal\n");
		return error;
	}

	//financial1��financial2����sectorid��totalbytenum��Ϊ0 �����

	if (0 == trace_io->totalbytenum)
		return success;

	//ͨ��traceIO�ṹ������IO��Ӧ��cache page id ��page num	//�������ʱ����;�������
	unsigned long long pageid = (trace_io->sectorid) * 512 / pagesize;
	unsigned long long pagenum = (trace_io->sectorid * 512 - pageid * pagesize + (trace_io->totalbytenum) - 1) / pagesize + 1;

	unsigned long i;
	for (i = 0; i < pagenum; i++)
	{
		//��ÿһ��page���д���
		if (error == each_page_execute(rwtype, head, (unsigned long)pageid + i))
			return error;
	}

	return success;
}

int main()
{
	//��ʼ������
	Node *head;
	head = List_head_init();

	hashtable_init();

	IOReq *req = (IOReq*)malloc(sizeof(IOReq));
	if (NULL == req)
	{
		printf("malloc ioreq failed\n");
		return;
	}

	//���cache


	//���ļ�
	FILE *fp;
	if (NULL == (fp = fopen("Financial2.spc", "r")))
	{
		printf("Error on open file!");
		return;
	}

	//����trace
	unsigned long i = 1;
	while (!feof(fp))
	{
		if (error == analysis_trace_IOreq(fp, req))
			break;
		if (error == each_trace_IO_execute(head, req)) 	//ִ��traceÿһ������
			return;
		printf("ing~~~%ld\n", i);
		i++;
	}

	printf("totalreadreq is %lld\n", totalreadreq);

	//���������ʣ�totalpage*pagesize/totaltime��
	unsigned long long latency;
	latency = totaltime / totalpage;
	printf("hit_readpage is %lld, no_hit_readpage is %lld\n", hit_readpage, no_hit_readpage);
	printf("totalpage is %lld, totaltime is %lld, latency is %lld\n", totalpage, totaltime,latency);

	//ע������
	fclose(fp);
	//system("pause");
}
