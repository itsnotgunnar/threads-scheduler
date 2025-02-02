#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_list
{
	struct test_list* pEvenNext; // All List pNext for even list
	struct test_list* pOddNext; // All List pNext for odd list
	struct test_list* pAllNext; // All List pNext for odd list
	int value;
} TestStructure;

typedef struct
{
	TestStructure* pHead;
	TestStructure* pTail;
	int count;
	//int offset; // Offset of ListNode within the structure
} List;

/* ---------------------------------------------------------------
	ListInitialize

	Purpose - Initialize a List type
	Parameters - nextPrevOffset - offset from beginning of
				structure to the next and previous pointers
				with the structure that makes up the nodes
	Returns - None
	Side Effects -
--------------------------------------------------------------- */
static void ListInitialize(List* pList)
{
	pList->pHead = pList->pTail = NULL;
	pList->count = 0;
}

/* ---------------------------------------------------------------
	ListAddNodeEvens

	Purpose - Adds a node to the end of the list
	Parameters - List *pList - pointer to the list
				TestStructure *pStructToAdd - pointer
				to the structure to add
	Returns - None
	Side Effects -
--------------------------------------------------------------- */
static void ListAddNodeEvens(List* pList, TestStructure* pStructToAdd)
{
	int listOffset;

	pStructToAdd->pEvenNext = NULL;

	if (pList->pHead == NULL)
	{
		// Set both the head and the tail pointer to the new node
		pList->pHead = pList->pTail = pStructToAdd;
	}
	else
	{
		// Move the tail after pointing to it with current tail's pNext
		pList->pTail->pEvenNext = pStructToAdd;
		pList->pTail = pStructToAdd;
	}
	pList->count++;
}

/* ---------------------------------------------------------------
	ListAddNodeOdds

	Purpose - Adds a node to the end of the list
	Parameters - List *pList - pointer to the list
				TestStructure *pStructToAdd - pointer
				to the structure to add
	Returns - None
	Side Effects -
--------------------------------------------------------------- */
static void ListAddNodeOdds(List* pList, TestStructure* pStructToAdd)
{
	int listOffset;

	pStructToAdd->pOddNext = NULL;

	if (pList->pHead == NULL)
	{
		// Set both the head and the tail pointer to the new node
		pList->pHead = pList->pTail = pStructToAdd;
	}
	else
	{
		// Move the tail after pointing to it with current tail's pNext
		pList->pTail->pOddNext = pStructToAdd;
		pList->pTail = pStructToAdd;
	}
	pList->count++;
}

/* ---------------------------------------------------------------
	ListAddNodeAll

	Purpose - Adds a node to the end of the list
	Parameters - List *pList - pointer to the list
				TestStructure *pStructToAdd - pointer
				to the structure to add
	Returns - None
	Side Effects -
--------------------------------------------------------------- */
static void ListAddNodeAll(List* pList, TestStructure* pStructToAdd)
{
	int listOffset;

	pStructToAdd->pAllNext = NULL;

	if (pList->pHead == NULL)
	{
		// Set both the head and the tail pointer to the new node
		pList->pHead = pList->pTail = pStructToAdd;
	}
	else
	{
		// Move the tail after pointing to it with current tail's pNext
		pList->pTail->pAllNext = pStructToAdd;
		pList->pTail = pStructToAdd;
	}
	pList->count++;
}

/* ---------------------------------------------------------------
	ListPopNodeEvens

	Purpose - Removes the first node from the list and returns
				a pointer to it
	Parameters - List *pList - pointer to the list
	Returns - A pointer to the removed node
	Side Effects -
--------------------------------------------------------------- */
static TestStructure* ListPopNodeEvens(List* pList)
{
	TestStructure* pNode = NULL;

	if (pList->count > 0)
	{
		pNode = pList->pHead;
		pList->pHead = pNode->pEvenNext;
		pList->count--;

		// Clear prev and next
		pNode->pEvenNext = NULL;

		// Clear the tail pointer if the list is now empty
		if (pList->count == 0)
		{
			pList->pHead = pList->pTail = NULL;
		}
	}
	return pNode;
}

/* ---------------------------------------------------------------
	ListPopNodeOdds

	Purpose - Removes the first node from the list and returns
				a pointer to it
	Parameters - List *pList - pointer to the list
	Returns - A pointer to the removed node
	Side Effects -
--------------------------------------------------------------- */
static TestStructure* ListPopNodeOdds(List* pList)
{
	TestStructure* pNode = NULL;

	if (pList->count > 0)
	{
		pNode = pList->pHead;
		pList->pHead = pNode->pOddNext;
		pList->count--;

		// Clear prev and next
		pNode->pOddNext = NULL;

		// Clear the tail pointer if the list is now empty
		if (pList->count == 0)
		{
			pList->pHead = pList->pTail = NULL;
		}
	}
	return pNode;
}

/* ---------------------------------------------------------------
	ListPopNodeAll

	Purpose - Removes the first node from the list and returns
				a pointer to it
	Parameters - List *pList - pointer to the list
	Returns - A pointer to the removed node
	Side Effects -
--------------------------------------------------------------- */
static TestStructure* ListPopNodeAll(List* pList)
{
	TestStructure* pNode = NULL;

	if (pList->count > 0)
	{
		pNode = pList->pHead;
		pList->pHead = pNode->pAllNext;
		pList->count--;

		// Clear prev and next
		pNode->pAllNext = NULL;

		// Clear the tail pointer if the list is now empty
		if (pList->count == 0)
		{
			pList->pHead = pList->pTail = NULL;
		}
	}
	return pNode;
}

/* ---------------------------------------------------------------
List Unit Testing
----------------------------------------------------------------*/

int main()
{
	TestStructure allPossibleNodes[100]; // Array of all possible nodes
	TestStructure* pTestStruct;

	// Three different lists
	List listEvens;
	List listOdds;
	List listAll;

	// Start with an empty array
	memset(allPossibleNodes, 0, sizeof(allPossibleNodes));

	ListInitialize(&listEvens);
	ListInitialize(&listOdds);
	ListInitialize(&listAll);

	srand(clock());

	// Generate random values and add to listAll and either listEven or listOdd
	for (int i = 0; i < 10; ++i)
	{
		allPossibleNodes[i].value = rand() % 100;
		ListAddNodeAll(&listAll, &allPossibleNodes[i]);

		if ((allPossibleNodes[i].value % 2) == 0)
		{
			ListAddNodeEvens(&listEvens, &allPossibleNodes[i]);
		}
		else
		{
			ListAddNodeOdds(&listOdds, &allPossibleNodes[i]);
		}
	}

	// Display what's in each of the lists
	printf("There are %d even values.\n", listEvens.count);
	while ((pTestStruct = ListPopNodeEvens(&listEvens)) != NULL)
	{
		printf("%d\n", pTestStruct->value);
	}

	printf("There are %d odd values.\n", listOdds.count);
	while ((pTestStruct = ListPopNodeOdds(&listOdds)) != NULL)
	{
		printf("%d\n", pTestStruct->value);
	}

	printf("There are %d values.\n", listAll.count);
	while ((pTestStruct = ListPopNodeAll(&listAll)) != NULL)
	{
		printf("%d\n", pTestStruct->value);
	}

	return 0;
}