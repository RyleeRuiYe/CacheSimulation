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

//全局变量(是否会存在问题?如果有问题，用指针一级一级往下传)
unsigned long long totalpage; //trace中总共涉及到的page个数
unsigned long long totaltime; //trace中总时间
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

//解析traceIO
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

//初始化hash表
void hashtable_init()
{
	int i;
	for (i = 0; i < hashsize; i++)
	{
		hashtable[i] = NULL;
	}
}

//初始化双向链表
Node *List_head_init()
{
	Node *headnode = (Node *)malloc(sizeof(Node));

	if (NULL == headnode)
	{
		printf("init list failed\n");
		return NULL;
	}

	headnode->pageid = 0; //头结点的pageid用于当前cache中page计数
	headnode->next = headnode;
	headnode->prev = headnode;
	return headnode;
}

//查找hash表，看是否命中
Hashnode *search_hash(unsigned long pageid)//输入pageid
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

//pageid命中处理流程：将该page移到头部,totalpage+1，totaltime+readlentency
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

//删除pagelist尾部对应的hashtable节点
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

//删除pagelist尾部节点
void delete_thelastnode(Node *head)
{
	//	printf("begin:delete_thelastnode\n");
	Node *node = head->prev;
	node->prev->next = head;
	head->prev = node->prev;
	head->pageid--;//list头结点计数减1
	free(node);
	//	printf("end:delete_thelastnode\n");
}

//添加一个新节点在pagelist首部
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

	if (head == head->next)//链表为空
	{
		head->next = new;
		head->prev = new;
		new->next = head;
		new->prev = head;
		head->pageid++;//计数加一
		return new;
	}

	head->next->prev = new;
	new->next = head->next;
	head->next = new;
	new->prev = head;
	head->pageid++;//计数加一
	return new;
}

//添加一个新项到hashtable
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

//在pagelist头部添加一个新的page，并且在hashtable中添加新项
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

//pageid不命中的处理流程：首先判断cache是否满了，满了就进行淘汰（淘汰尾部），不满就直接添加（加到头部）；totalpage+1，totaltime+writelentency
int page_not_hit(Node *head, unsigned long id)
{
	//	printf("begin:page_not_hit\n");
	unsigned long pagemaxnum = cachesize / pagesize;
	if (head->pageid < pagemaxnum)
	{
		//添加
		if (error == insert_new_to_pagelist_and_hashtable(head, id))
			return error;
	}
	else
	{
		//先淘汰		
		delete_hashnode(head->prev->pageid);
		delete_thelastnode(head);//head是pagelist头结点
		//添加
		if (error == insert_new_to_pagelist_and_hashtable(head, id))
			return error;
	}

	return success;
}

int each_page_execute(char rwtype, Node* head, unsigned long pageid)
{
	//	printf("begin:each_page_executen");
	//判断操作类型
	if ('R' == rwtype || 'r' == rwtype)
	{
		Hashnode *hashnode = search_hash(pageid);
		if (NULL != hashnode)
		{
			//命中
			move_page_to_head(head, hashnode->thepage);//head，pagelist头结点
			totalpage++;
			hit_readpage++;
			totaltime += readlentency;
		}
		else
		{
			//不命中
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

	//financial1和financial2中有sectorid和totalbytenum均为0 的情况

	if (0 == trace_io->totalbytenum)
		return success;

	//通过traceIO结构体计算该IO对应的cache page id 和page num	//这里计算时候中途会有溢出
	unsigned long long pageid = (trace_io->sectorid) * 512 / pagesize;
	unsigned long long pagenum = (trace_io->sectorid * 512 - pageid * pagesize + (trace_io->totalbytenum) - 1) / pagesize + 1;

	unsigned long i;
	for (i = 0; i < pagenum; i++)
	{
		//对每一个page进行处理
		if (error == each_page_execute(rwtype, head, (unsigned long)pageid + i))
			return error;
	}

	return success;
}

int main()
{
	//初始化操作
	Node *head;
	head = List_head_init();

	hashtable_init();

	IOReq *req = (IOReq*)malloc(sizeof(IOReq));
	if (NULL == req)
	{
		printf("malloc ioreq failed\n");
		return;
	}

	//填充cache


	//打开文件
	FILE *fp;
	if (NULL == (fp = fopen("Financial2.spc", "r")))
	{
		printf("Error on open file!");
		return;
	}

	//解析trace
	unsigned long i = 1;
	while (!feof(fp))
	{
		if (error == analysis_trace_IOreq(fp, req))
			break;
		if (error == each_trace_IO_execute(head, req)) 	//执行trace每一条请求
			return;
		printf("ing~~~%ld\n", i);
		i++;
	}

	printf("totalreadreq is %lld\n", totalreadreq);

	//计算吞吐率（totalpage*pagesize/totaltime）
	unsigned long long latency;
	latency = totaltime / totalpage;
	printf("hit_readpage is %lld, no_hit_readpage is %lld\n", hit_readpage, no_hit_readpage);
	printf("totalpage is %lld, totaltime is %lld, latency is %lld\n", totalpage, totaltime,latency);

	//注销操作
	fclose(fp);
	//system("pause");
}
