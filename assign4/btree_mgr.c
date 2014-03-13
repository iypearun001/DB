#include "dberror.h"
#include "btree_mgr.h"
#include "tables.h"
#include "expr.h"
#include "string.h"
#include "assert.h"

typedef struct BT_MgmtData {
    PageNumber rootPage;
    int nodeCount;
    int entryCount;

    int keyType;
    int order;

    BM_BufferPool bm;
    BM_PageHandle ph;
} BT_MgmtData;

typedef struct BT_NodeElement {
	long long ptr; // if (leaf) ptr is a PageNumber
	               // else ptr is a RID
                   // long long = 8 bytes = sizeof(RID)
	Value key;
} BT_NodeElement;
// We keep elements in BT_Node sorted by keys.
// Sorting is done by moving elements. We can consider
// using 2 more points like BT_NodeElement *next, *prev in
// BT_NodeElement and build sorted list without moving elements.
// Trade-off is space vs CPU time. Optimizing for space for now.

// Node means a page in page file/buffer
// BT_Node { ... el[0], el[1], el[2]... el[n-1], el[n] }
// 'nodePtr' points points to right leaf, if BT_Node is leaf
// 'nodePtr' points points to right down, if BT_Node is nonleaf
// max 'n'= order, for leaf
// max 'n'= order-1, for non-leaf
typedef struct BT_Node {
	bool leaf;
	int numKeys;
	PageNumber parent;
	PageNumber nodePtr;   // Points to either next leaf, or child node.
    BT_NodeElement el; // First element in this node. 
                       // This should be last member.
} BT_Node;

typedef struct BT_ScanMgmtData {
    PageNumber pn;
    BT_Node *curNode;
    int curPos;
} BT_ScanMgmtData;

// Supporting MACROS
#define MAX_ELEMENTS()  (((PAGE_SIZE - sizeof(BT_Node))/sizeof(BT_NodeElement))-1)
#define IS_TRUE(tres)    (tres.v.boolV)
#define IS_FALSE(tres)   (!tres.v.boolV)
#define SPLIT_POINT(o)  ((o+1)/2)
#define MIN_KEYS(t,n)   SPLIT_POINT(t->order)
#define	CAPACITY(t)   (t->order)
#define MAX_TREE_DEPTH  10000

// Supporting functions
// SCAN
static BT_Node* findElement (BTreeHandle *tree, BT_Node *node, Value *key,
                             int *elemPos, int *pageNum,
                             bool even_if_dont_match);

// INSERT
static int addKeyInNode(BTreeHandle *tree, BT_Node *node, long long ptr, Value *key);
static int delKeyFromNode(BTreeHandle *tree, BT_Node *node, PageNumber ptr, Value *key);
static int createBTNode(BTreeHandle *tree);
static RC insertKeyInParent(BTreeHandle *tree, PageNumber leftPn, 
                            PageNumber rightPn, Value key);
static RC splitAndInsertKey(BTreeHandle *tree, PageNumber pn, bool leaf);
static RC mergeElements(BTreeHandle *tree, PageNumber lpn, PageNumber rpn);
static RC distributeElements(BTreeHandle *tree, PageNumber lpn, PageNumber rpn);

static BT_Node* getPinnedBTNode(BTreeHandle *tree, PageNumber pn)
{
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    BM_PageHandle ph;

    pinPage(&btmd->bm, &ph, (PageNumber)pn);
    return( (BT_Node*) ph.data);
}

static void unpinBTNode(BTreeHandle *tree, PageNumber pn)
{
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    BM_PageHandle ph;

    ph.pageNum= pn;
    unpinPage(&btmd->bm, &ph);
}

// DELETE

// init and shutdown index manager
RC initIndexManager (void *mgmtData)
{
    RC rc;
    // Initialized ?
    if ((rc=isStorageManagerInitialized()) == RC_OK)
        return(rc);
 
    // Init storage manager
    initStorageManager();

    RETURN(RC_OK);
}

RC shutdownIndexManager ()
{
    RC rc;
    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);
 
    // Error if there are open tables
    if ((rc=shutdownStorageManager()) == RC_OK)
        RETURN(rc);

    RETURN(RC_OK);
}

// create, destroy, open, and close an btree index
RC createBtree (char *idxId, DataType keyType, int n)
{
    SM_FileHandle fh;
    char data[PAGE_SIZE];
    char *offset= data;
    RC rc;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    // Stop if 'n' (the order) is too high to fit in a page
    if ( n > MAX_ELEMENTS())
        RETURN(RC_ORDER_TOO_HIGH_FOR_PAGE);

    // rootPage, nodeCount, entryCount, keytype, order
    memset(offset, 0, PAGE_SIZE);
    *(int*)offset = 1; // At the begining page 1 contains root node.
                       // Whenever new root is generated, we update this page.
    offset+= sizeof(int);

    *(int*)offset = 0;
    offset+= sizeof(int);

    *(int*)offset = 0;
    offset+= sizeof(int);

    *(int*)offset = keyType;
    offset+= sizeof(int);

    *(int*)offset = n;
    offset+= sizeof(int);

    // Create a file with 1 page index data
    if ((rc=createPageFile(idxId)) != RC_OK)
        return rc;

    if ((rc=openPageFile(idxId, &fh)) != RC_OK)
        return rc;

    if ((rc=writeBlock(0, &fh, data)) != RC_OK)
        return rc;

    if ((rc=closePageFile(&fh)) != RC_OK)
        return rc;

    RETURN(RC_OK);
}

RC openBtree (BTreeHandle **tree, char *idxId)
{
    BT_MgmtData *btmd;
    RC rc;
    char *offset;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    // Allocate RM_TableData
    *tree= (BTreeHandle*) malloc( sizeof(BTreeHandle) );
    memset(*tree, 0, sizeof(BTreeHandle));
    btmd= (BT_MgmtData*) malloc( sizeof(BT_MgmtData) );
    memset(btmd, 0, sizeof(BT_MgmtData));
    (*tree)->mgmtData= (void*) btmd;

    // Setup BM
    if ( (rc=initBufferPool(&btmd->bm, idxId, 1000, 
                            RS_LRU, NULL)) != RC_OK)
        return rc;

    // Read page and prepare schema
    offset= (char*) getPinnedBTNode(*tree, (PageNumber)0);
    btmd->rootPage= *(PageNumber*)offset;
    offset+= sizeof(int);
    btmd->nodeCount= *(int*)offset;
    offset+= sizeof(int);
    btmd->entryCount= *(int*)offset;
    offset+= sizeof(int);

    btmd->keyType= *(int*)offset;
    offset+= sizeof(int);
    btmd->order= *(int*)offset;
    offset+= sizeof(int);
    // Unpin after reading
    unpinBTNode(*tree, (PageNumber)0);

    RETURN(RC_OK);
}

RC closeBtree (BTreeHandle *tree)
{
    BT_MgmtData *btmd;
    char *offset;
    RC rc;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    btmd= (BT_MgmtData*) tree->mgmtData;

    // Read page and prepare schema
    offset= (char*) getPinnedBTNode(tree, (PageNumber)0);

    markDirty(&btmd->bm, &btmd->ph);
    *(int*)offset= btmd->rootPage;
    offset+= sizeof(int);
    *(int*)offset= btmd->nodeCount;
    offset+= sizeof(int);
    *(int*)offset= btmd->entryCount;
    offset+= sizeof(int);
    unpinBTNode(tree, (PageNumber)0);

    // CloseBM
    shutdownBufferPool(&btmd->bm);
    
    free(btmd);
    free(tree);

    RETURN(RC_OK);
}

RC deleteBtree (char *idxId)
{
    RC rc;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);
 
    // Destroy file
    if ((rc=destroyPageFile(idxId)) != RC_OK)
        return rc;

    RETURN(RC_OK);
}

// access information about a b-tree
RC getNumNodes (BTreeHandle *tree, int *result)
{
    BT_MgmtData *btmd;
    RC rc;

    btmd= (BT_MgmtData*) tree->mgmtData;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    *result= btmd->nodeCount;

    RETURN(RC_OK);
}
RC getNumEntries (BTreeHandle *tree, int *result)
{
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    RC rc;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    *result= btmd->entryCount;

    RETURN(RC_OK);
}
RC getKeyType (BTreeHandle *tree, DataType *result)
{
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    RC rc;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    *result= btmd->keyType;

    RETURN(RC_OK);
}

// index access
BT_Node* findElement (BTreeHandle *tree, BT_Node *node, Value *key,
                      int *elemPos, int *pageNum, bool even_if_dont_match)
{
    int cnt= 0;
    Value res, eqRes;
    int isGreater;
    BT_Node *n;
    PageNumber pn;
    BT_NodeElement *el;

    // If node has no elements
    if (!node->numKeys) return (NULL);

    // Do we have it in current node ?
    el= &node->el;
    while (cnt < node->numKeys) // Single node traversal
    {
        valueSmaller(&el[cnt].key, key, &res);
        valueEquals(&el[cnt].key, key, &eqRes);
        isGreater= IS_FALSE(res) && IS_FALSE(eqRes);

        if (node->leaf) // Leaf
        {

            // STOP if,
            // - equals
            // - greater
            if (IS_TRUE(eqRes))
            {
                *elemPos= cnt;
                return(node);
            }
            if (isGreater) 
            {
                if(even_if_dont_match)
                {
                  *elemPos= cnt;
                  return(node);
                }
                *elemPos= *pageNum= -1;
                return(NULL);
            }
        } 
        else if (isGreater) // NonLeaf
        {
            PageNumber pn= (PageNumber) el[cnt].ptr;
            n= getPinnedBTNode(tree, pn);
            *pageNum= pn;
            node= findElement (tree, n, key, elemPos, pageNum, even_if_dont_match);
            unpinBTNode(tree, pn);
            return(node);
        }

        cnt++;
    } // cnt == numKeys after this loop

    // Stop if we reached leaf
    if (node->leaf) 
    {
        if (even_if_dont_match)
        {
            *elemPos= cnt;
            return(node);
        }
        *elemPos= *pageNum= -1;
        return(NULL);
    }

    // Traverse right side down node
    pn= node->nodePtr;
    n= getPinnedBTNode(tree, pn);
    *pageNum= pn;
    node= findElement (tree, n, key, elemPos, pageNum, even_if_dont_match);
    unpinBTNode(tree, pn);

    return(node);
}

RC findKey (BTreeHandle *tree, Value *key, RID *result)
{
    int elemPos= -1;
    RC rc;
    BT_Node *n, *node;
    BT_NodeElement *el;
    PageNumber pn, pnRes;

    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    // Search if element exists
    pn= btmd->rootPage;
    n= getPinnedBTNode(tree, pn);
    pnRes= pn;
    node= findElement (tree, n, key, &elemPos, &pnRes, 0);
    unpinBTNode(tree, pn);

    // Search failed
    if (!node)
      RETURN(RC_IM_KEY_NOT_FOUND);

    // Search succeeded
    n= getPinnedBTNode(tree, pnRes);
    el= &node->el;
    *result= *((RID*) &el[elemPos].ptr);
    unpinBTNode(tree, pnRes);

    RETURN(RC_OK);
}


// INSERT ****

// We don't check limit here, so check before calling this
// We do sorted insert
// Caller should have pinned 'n'
static int addKeyInNode(BTreeHandle *tree, BT_Node *node, long long ptr, Value *key)
{
    int cnt=0;
    BT_NodeElement *el;
    BT_MgmtData *btmd;
    btmd= (BT_MgmtData*) tree->mgmtData;
    Value res;

    // Search till we find good position (sorting)
    el= &node->el;
    while(cnt < node->numKeys)
    {
        valueSmaller(&el[cnt].key, key, &res);
        if (IS_FALSE(res))
            break;
        cnt++;
    }
    // Move other elements to make room
    if (cnt < node->numKeys)
      memmove((char*) &el[cnt+1], (char*) &el[cnt],
              (int) (node->numKeys-cnt+1)*sizeof(BT_NodeElement));

    // Store and increment count.
    el[cnt].ptr= ptr;
    el[cnt].key= *key;
    if (node->leaf)
       btmd->entryCount++;
    node->numKeys++;

    return(cnt);
}

// Create new BT_Node 
static int createBTNode(BTreeHandle *tree)
{
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    BM_Pool_MgmtData *pmd= btmd->bm.mgmtData;

    // Create new node page
    if (appendEmptyBlock(&pmd->fh) != RC_OK)
        RETURN(RC_RM_INSERT_FAILED);

    // Increase node count
    btmd->nodeCount++;
    return(pmd->fh.totalNumPages-1);
}

// Add new parent BT_Node and store pointers for left and right.
static RC insertKeyInParent(BTreeHandle *tree, PageNumber leftPn, 
                            PageNumber rightPn, Value key)
{
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    BT_Node *parent, *left, *right;
    BT_NodeElement *el;
    PageNumber pn;
    int cnt=0, parentKeys;

    left= getPinnedBTNode(tree, leftPn);
    right= getPinnedBTNode(tree, rightPn);

    // New Root
    if (left->parent == -1)
    {
        pn= createBTNode(tree);
        btmd->rootPage= pn;

        // Store the key
        parent= getPinnedBTNode(tree, pn);
        parent->parent= -1;
        parent->nodePtr= -1;
        parent->leaf= 0;
        parent->numKeys= 0;
    }
    else // Read current parent
    {
        pn= left->parent;
        parent= getPinnedBTNode(tree, pn);
    }

    // Find space for key in parent
    if (left->leaf)
    {
        el= &right->el;
        key= el[0].key; 
    }
    cnt= addKeyInNode(tree, parent, leftPn, &key);
    parentKeys=parent->numKeys;
    if (cnt == parentKeys-1)
      parent->nodePtr= rightPn;
    else
    {
        el= &parent->el;
        el[cnt+1].ptr= rightPn;
    }

    left->parent= right->parent= pn;

    unpinBTNode(tree, pn);
    unpinBTNode(tree, leftPn);
    unpinBTNode(tree, rightPn);

    // Simple Insert
    if (parentKeys <= CAPACITY(btmd))
    {
       unpinBTNode(tree, pn);
       RETURN(RC_OK);
    }
    unpinBTNode(tree, pn);

    // Insert and then slipt
    return (splitAndInsertKey(tree, pn, 0));
}

// Create two BT_Nodes, copy half of elements into new
// then add new parent.
// 'pn' points to page for the BT_Node that we are splitting.
static RC splitAndInsertKey(BTreeHandle *tree, PageNumber lpn, bool leaf)
{
    char *dest_el, *src_el;
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    int copyCnt, ignore=0;
    Value splitKey;
    PageNumber splitPtr;

    BT_Node *left, *right;
    BT_NodeElement *rEl, *lEl;
    PageNumber rpn;

    left= getPinnedBTNode(tree, lpn);
    lEl= &left->el;

    // Create new left node and shift elements
    rpn= createBTNode(tree);
    right= getPinnedBTNode(tree, rpn);
    right->parent= left->parent;
    right->leaf= leaf;
    rEl= &right->el;

    // Copy half elements adjust nodePtr
    copyCnt= SPLIT_POINT(btmd->order);
    right->nodePtr= left->nodePtr; // before moving it still points to current
    if (!leaf)
    {
        //the first element in right node should go in parent
        splitKey= lEl[left->numKeys-copyCnt-1].key;
        splitPtr= lEl[left->numKeys-copyCnt-1].ptr;
        ignore= 1;

        left->nodePtr= splitPtr;
    }
    else
      left->nodePtr= rpn;


    // Move elements from left to right.
    dest_el= (char*) &rEl[0];
    src_el= (char*) &lEl[left->numKeys-copyCnt];
    right->numKeys= copyCnt;
    memcpy(dest_el, src_el, right->numKeys*(sizeof(BT_NodeElement)));

    left->numKeys -= copyCnt+ignore;

    unpinBTNode(tree, lpn);
    unpinBTNode(tree, rpn);

    // Insert element in parent
    return(insertKeyInParent(tree, lpn, rpn, splitKey));
}

// User calls this one
RC insertKey (BTreeHandle *tree, Value *key, RID rid)
{
    RC rc;
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;

    int elemPos= -1, cnt;
    BT_Node *node, *newNode;
    BT_NodeElement *el;
    Value res;
    PageNumber newPn, pn;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    // If fresh tree
    if (!btmd->entryCount)
    {
       newPn= createBTNode(tree);
       btmd->rootPage= newPn;

       // Store the key
       newNode= getPinnedBTNode(tree, newPn);
       newNode->leaf= 1;
       newNode->parent= -1;
       newNode->nodePtr= -1;
       addKeyInNode(tree, newNode, *((long long*) &rid), key);
       unpinBTNode(tree, newPn);

       RETURN(RC_OK);
    }

    // Find good place to insert
    node= getPinnedBTNode(tree, btmd->rootPage);
    pn= btmd->rootPage;
    node= findElement (tree, node, key, &elemPos, &pn, 1);
    el= &node->el;

    // Stop if we already have element
    valueEquals(&el[elemPos].key, key, &res);
    unpinBTNode(tree, btmd->rootPage);
    if (IS_TRUE(res))
      RETURN(RC_IM_KEY_ALREADY_EXISTS);

    // Add key
    node= getPinnedBTNode(tree, pn);
    cnt= addKeyInNode(tree, node, *((long long*) &rid), key);

    // Simple Insert
    // el[elemPos] in 'node' is right place to insert
    if (node->numKeys <= CAPACITY(btmd))
    {
       unpinBTNode(tree, pn);
       RETURN(RC_OK);
    }
    unpinBTNode(tree, pn);

    // Needs split
    return (splitAndInsertKey(tree, pn, 1));
}

// DELETE ****
static int delKeyFromNode(BTreeHandle *tree, BT_Node *node, PageNumber ptr, Value *key)
{
    int cnt=0;
    BT_NodeElement *el;
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    Value res;

    el= &node->el;
    while(cnt < node->numKeys)
    {
        if (node->leaf)
        {
            valueEquals(&el[cnt].key, key, &res);
            if (IS_TRUE(res))
            {
                if (cnt < node->numKeys-1)
                    memmove((char*) &el[cnt], (char*) &el[cnt+1],
                            (int) (node->numKeys-cnt+1)*sizeof(BT_NodeElement));
                node->numKeys--;
                btmd->entryCount--;

                return(cnt);
            }
        }
        else
        {
            valueEquals(&el[cnt].key, key, &res);
            if (IS_TRUE(res) || el[cnt].ptr==ptr)
            {
                PageNumber tmpPn;
                if (cnt==0)
                    tmpPn= el[0].ptr;

                memmove((char*) &el[cnt], (char*) &el[cnt+1],
                        (int) (node->numKeys-cnt+1)*sizeof(BT_NodeElement));
                if (cnt==0)
                    el[0].ptr= tmpPn;

                node->numKeys--;
                btmd->entryCount--;

                return(cnt);
            }
            if (cnt+1 == node->numKeys && node->nodePtr == ptr)
            {
                node->numKeys--;
                btmd->entryCount--;
                return(cnt+1);
            }
        }
        cnt++;
    }
    if (node->leaf)
      assert(!"We should not be here");
    return (0);
}

// Prefer left, else right
// returns -1 if parent is root.
// returns 0 if this alone
static PageNumber getNeighborNode(BTreeHandle *tree, PageNumber pn)
{
    BT_Node *parent, *tmp;
    BT_NodeElement *el;
    PageNumber neighborPn;
    int cnt, parentPage;

    tmp= getPinnedBTNode(tree, pn);
    parentPage= tmp->parent;
    unpinBTNode(tree, pn);
    if (parentPage < 0)
        return 0; // should not hit this

    parent= getPinnedBTNode(tree, parentPage);

    el= &parent->el;
    for (cnt=0; cnt<parent->numKeys; cnt++)
        if (pn == el[cnt].ptr)
            break;

    // Get neighbor page number
    if (cnt == parent->numKeys && pn == parent->nodePtr )
        neighborPn= el[parent->numKeys-1].ptr;
    else if (cnt == parent->numKeys-1)
        neighborPn= -parent->nodePtr;
    else if (cnt == 0 )
        neighborPn= -el[cnt+1].ptr;     // Right neighbor
    else
        neighborPn= el[cnt-1].ptr; // Left neighbor

    unpinBTNode(tree, parentPage);

    return neighborPn;
}

static RC deleteElement(BTreeHandle *tree, PageNumber fromPn, Value *key)
{
    int neighborKeys, remainingKeys, cnt;
    BT_Node *node, *neighborNode, *parentNode, *tmpNode;
    BT_NodeElement *el;
    PageNumber neighborPn, parentPn;
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    RC rc= RC_OK;

    // Delete the element from BTNode
    node= getPinnedBTNode(tree, fromPn);
    parentPn= node->parent;
    cnt= delKeyFromNode(tree, node, fromPn, key); // search by ptr on nonleaf
    remainingKeys= node->numKeys;
    unpinBTNode(tree, fromPn);

    // Special case
    if (parentPn>0)
    {
        parentNode= getPinnedBTNode(tree, parentPn);
        if (node->numKeys && cnt==0)
        {
            Value eqRes;
            // just update parent with new first element
            el= &parentNode->el;
            for(cnt=0; cnt<parentNode->numKeys; cnt++)
            {
                valueEquals(&el[cnt].key, key, &eqRes);
                if (IS_TRUE(eqRes))
                {
                    el[cnt].key = node->el.key;
                    break;
                }
            }
        }
        else if(node->numKeys==0 && parentNode->numKeys==1) // Remove root
        {
            if (fromPn == parentNode->el.ptr)
                btmd->rootPage= parentNode->nodePtr;
            else
                btmd->rootPage= parentNode->el.ptr;
            node->parent= -1;
            btmd->nodeCount--;
            parentNode->numKeys--;
            if (node->leaf)
                node->nodePtr= -1;
        }
        unpinBTNode(tree, parentPn);
    }

    // We need not worry about merge/distribute
    // if we are at root node.
    if (btmd->rootPage == fromPn || node->parent < 0)
    {
        // If root is completely empty then
        // we reset all counters
        if (btmd->entryCount == 0)
        {
            btmd->rootPage= 1;
            btmd->entryCount= 0;
            btmd->nodeCount= 0;
        }

        // Special case - remove root
        if (node->numKeys == 1 && node->parent < 0)
        {
            tmpNode= getPinnedBTNode(tree, node->el.ptr);
            if (tmpNode->numKeys==0)
            {
                btmd->rootPage= node->nodePtr;
                tmpNode->parent= -1;
                btmd->nodeCount--;
                if (tmpNode->leaf)
                    tmpNode->nodePtr= -1;
            }
            unpinBTNode(tree, node->el.ptr);
            tmpNode= getPinnedBTNode(tree, node->nodePtr);
            if (tmpNode->numKeys==0)
            {
                btmd->rootPage= node->el.ptr;
                tmpNode->parent= -1;
                btmd->nodeCount--;
                if (tmpNode->leaf)
                    tmpNode->nodePtr= -1;
            }
            unpinBTNode(tree, node->nodePtr);
        }
        RETURN(RC_OK);
    }

    // Was that simple delete ?
    if (remainingKeys > MIN_KEYS(btmd,node))
        RETURN(RC_OK);

    // Find neighbor BT_Node
    neighborPn= getNeighborNode(tree, fromPn);
    neighborNode= getPinnedBTNode(tree, (PageNumber) neighborPn<0?neighborPn*-1:neighborPn);
    neighborKeys= neighborNode->numKeys;
    unpinBTNode(tree, (PageNumber) neighborPn);

    // Merge ?
    if ((neighborKeys+remainingKeys) <= CAPACITY(btmd))
       rc= mergeElements(tree, neighborPn, fromPn);

    // Distribute
    else if (remainingKeys < MIN_KEYS(btmd, node))
       rc= distributeElements(tree, neighborPn, fromPn);

    RETURN(rc);
}

static RC distributeElements(BTreeHandle *tree, PageNumber lpn, PageNumber rpn)
{
    BT_Node *l, *r;
    BT_NodeElement *lEl, *rEl;
    bool mergeRight= (lpn < 0);
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    int mergePtr;
    Value mergeKey;

    lpn= mergeRight ? (lpn*-1) : lpn; // ABS()
    l= getPinnedBTNode(tree, lpn);
    r= getPinnedBTNode(tree, rpn);
    lEl= &l->el;
    rEl= &r->el;

    // Leaf distribute
    if (l->leaf)
    {
        mergeKey= rEl[0].key;
        mergePtr= r->parent;

        // Move 1 element from right to left
        memcpy((char*) &lEl[l->numKeys], (char*) &rEl[0],
               (int) sizeof(BT_NodeElement));
        if (mergeRight) // special case
        {
            char *tmp= (char*) malloc(l->numKeys*sizeof(BT_NodeElement));
            memcpy(tmp, (char*) &lEl[0], l->numKeys*sizeof(BT_NodeElement));
            memcpy((char*) &lEl[0], (char*) &rEl[0], sizeof(BT_NodeElement));
            memcpy((char*) &lEl[1], (char*) tmp, l->numKeys*sizeof(BT_NodeElement));
        }
        else // move left
          l->nodePtr= r->nodePtr;

        l->numKeys+= 1;
        r->numKeys-= 1;

        unpinBTNode(tree, lpn);
        unpinBTNode(tree, rpn);

        RETURN(deleteElement(tree, mergePtr, &mergeKey));
    }
    /* Non Leaf merge - TODO nonleaf distribution
    else 
    {
        BT_NodeElement *el;
        BT_Node *tmp;
        BT_Node *parentNode;
        int parentIdx;

        // Get neighbor element from parent
        parentNode= getPinnedBTNode(tree, r->parent);
        el= &parentNode->el;
        for(cnt=0; cnt<parentNode->numKeys; cnt++)
            if (el[cnt].ptr == rpn)
            {
              parentIdx= cnt;
              break;
            }

        // First copy k,p from parent into left node.
        lEl[l->numKeys].key= el[parentIdx].key;
        lEl[l->numKeys].ptr= l->nodePtr;
        l->numKeys++;

        // Move all the elements from right to left
        memcpy((char*) &lEl[l->numKeys], (char*) &rEl[0],
               (int) r->numKeys*sizeof(BT_NodeElement));
        if (mergeRight) // special case
        {
            char *tmp= (char*) malloc(l->numKeys*sizeof(BT_NodeElement));
            memcpy(tmp, (char*) &lEl[0], 
                   l->numKeys*sizeof(BT_NodeElement));
            memcpy((char*) &lEl[0], (char*) &rEl[0], 
                   r->numKeys*sizeof(BT_NodeElement));
            memcpy((char*) &lEl[r->numKeys], (char*) tmp, 
                   l->numKeys*sizeof(BT_NodeElement));
        }
        else // move left
          l->nodePtr= r->nodePtr;

        mergeKey= rEl[0].key;
        mergePtr= r->parent;
        l->numKeys+= r->numKeys;

        // Free right node - TODO-Need to reuse these pages
        r->numKeys= 0;
        btmd->nodeCount--;

        // Update parent of all childs in left node (for which we copied)
        for(cnt=0; cnt<l->numKeys; cnt++)
        {
           tmp= getPinnedBTNode(tree, lEl[cnt].ptr);
           tmp->parent= lpn;
           unpinBTNode(tree, lEl[cnt].ptr);
        }
    
        unpinBTNode(tree, r->parent);
        unpinBTNode(tree, lpn);
        unpinBTNode(tree, rpn);

        RETURN(deleteElement(tree, mergePtr, &mergeKey));
    } */
    return(RC_OK);
}

static RC mergeElements(BTreeHandle *tree, PageNumber lpn, PageNumber rpn)
{
    BT_Node *l, *r, *tmpNode;
    BT_NodeElement *lEl, *rEl;
    bool mergeRight= (lpn < 0);
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    int cnt=0, mergePtr;
    Value mergeKey;

    lpn= mergeRight ? (lpn*-1) : lpn; // ABS()
    l= getPinnedBTNode(tree, lpn);
    r= getPinnedBTNode(tree, rpn);
    lEl= &l->el;
    rEl= &r->el;

    // Leaf merge
    if (l->leaf)
    {
        mergeKey= rEl[0].key;
        mergePtr= r->parent;

        // Move all the elements from right to left
        memcpy((char*) &lEl[l->numKeys], (char*) &rEl[0],
               (int) r->numKeys*sizeof(BT_NodeElement));
        if (mergeRight) // special case
        {
            char *tmp= (char*) malloc(l->numKeys*sizeof(BT_NodeElement));
            memcpy(tmp, (char*) &lEl[0], 
                   l->numKeys*sizeof(BT_NodeElement));
            memcpy((char*) &lEl[0], (char*) &rEl[0], 
                   r->numKeys*sizeof(BT_NodeElement));
            memcpy((char*) &lEl[r->numKeys], (char*) tmp, 
                   l->numKeys*sizeof(BT_NodeElement));
        }
        else // move left
          l->nodePtr= r->nodePtr;

        l->numKeys+= r->numKeys;

        // Update parent - find r and replace it by l
        Value eqRes;
        tmpNode= getPinnedBTNode(tree, r->parent);
        // just update parent with new first element
        if (tmpNode->nodePtr == rpn)
            tmpNode->nodePtr = lpn;
        unpinBTNode(tree, r->parent);

        // Free right node - TODO-Need to reuse these pages
        r->numKeys= 0;
        btmd->nodeCount--;

        unpinBTNode(tree, lpn);
        unpinBTNode(tree, rpn);

        RETURN(deleteElement(tree, mergePtr, &mergeKey));
    }
    // Non Leaf merge
    else if ( (l->numKeys+r->numKeys) < CAPACITY(btmd) ) // We need extra space for nonleaf merge
    {
        BT_NodeElement *el;
        BT_Node *tmp;
        BT_Node *parentNode;
        int parentIdx;

        // Get neighbor element from parent
        parentNode= getPinnedBTNode(tree, r->parent);
        el= &parentNode->el;
        for(cnt=0; cnt<parentNode->numKeys; cnt++)
            if (el[cnt].ptr == rpn)
            {
              parentIdx= cnt;
              break;
            }

        // First copy k,p from parent into left node.
        lEl[l->numKeys].key= el[parentIdx].key;
        lEl[l->numKeys].ptr= l->nodePtr;
        l->numKeys++;

        // Move all the elements from right to left
        memcpy((char*) &lEl[l->numKeys], (char*) &rEl[0],
               (int) r->numKeys*sizeof(BT_NodeElement));
        if (mergeRight) // special case
        {
            char *tmp= (char*) malloc(l->numKeys*sizeof(BT_NodeElement));
            memcpy(tmp, (char*) &lEl[0], 
                   l->numKeys*sizeof(BT_NodeElement));
            memcpy((char*) &lEl[0], (char*) &rEl[0], 
                   r->numKeys*sizeof(BT_NodeElement));
            memcpy((char*) &lEl[r->numKeys], (char*) tmp, 
                   l->numKeys*sizeof(BT_NodeElement));
        }
        else // move left
          l->nodePtr= r->nodePtr;

        mergeKey= rEl[0].key;
        mergePtr= r->parent;
        l->numKeys+= r->numKeys;

        // Free right node - TODO-Need to reuse these pages
        r->numKeys= 0;
        btmd->nodeCount--;

        // Update parent of all childs in left node (for which we copied)
        for(cnt=0; cnt<l->numKeys; cnt++)
        {
           tmp= getPinnedBTNode(tree, lEl[cnt].ptr);
           tmp->parent= lpn;
           unpinBTNode(tree, lEl[cnt].ptr);
        }
    
        unpinBTNode(tree, r->parent);
        unpinBTNode(tree, lpn);
        unpinBTNode(tree, rpn);

        RETURN(deleteElement(tree, mergePtr, &mergeKey));
    }
    return(RC_OK);
}

RC deleteKey (BTreeHandle *tree, Value *key)
{
    BT_MgmtData *btmd= (BT_MgmtData*) tree->mgmtData;
    PageNumber pnRes;
    int elemPos;
    BT_Node *n, *node;
    RC rc;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    // Find if element exists ?
    n= getPinnedBTNode(tree, btmd->rootPage);
    pnRes= btmd->rootPage;
    node= findElement (tree, n, key, &elemPos, &pnRes, 0);
    unpinBTNode(tree, btmd->rootPage);
    if (!node)
        RETURN(RC_IM_KEY_NOT_FOUND);

    // Delete the element
    RETURN(deleteElement(tree, pnRes, key));
}

// SCAN ****
RC openTreeScan (BTreeHandle *tree, BT_ScanHandle **handle)
{
    RC rc;
    BT_ScanMgmtData *btsmd;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    (*handle) = (BT_ScanHandle*) malloc(sizeof(BT_ScanHandle));
    memset(*handle, 0, sizeof(BT_ScanHandle));

    btsmd= (BT_ScanMgmtData*) malloc(sizeof(BT_ScanMgmtData));
    memset(btsmd, 0, sizeof(BT_ScanMgmtData));

    (*handle)->tree= tree;
    (*handle)->mgmtData= btsmd;
    btsmd->pn= -1;
    btsmd->curPos= -1;

    return(RC_OK);
}
RC nextEntry (BT_ScanHandle *handle, RID *result)
{
    RC rc;
    BT_ScanMgmtData *btsmd= handle->mgmtData;
    BT_MgmtData *btmd= handle->tree->mgmtData;
    BT_Node *n;
    BT_NodeElement *el;
    PageNumber pn, tmp;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    if (!btmd->entryCount)
        RETURN(RC_IM_NO_MORE_ENTRIES);

    // Go down and get 1st left most leaf.
    if (btsmd->pn == -1)
    {
        pn= btmd->rootPage;
        n= getPinnedBTNode(handle->tree, pn);
        while (!n->leaf)
        {
           tmp= n->el.ptr;
           unpinBTNode(handle->tree, pn);
           pn= tmp;
           n= getPinnedBTNode(handle->tree, pn);
        }

        btsmd->pn= pn; // The first page to get elements from.
        btsmd->curPos= 0;
        btsmd->curNode= n;
    }

    // Should we go to next node ?
    n= btsmd->curNode;
    if (btsmd->curPos == n->numKeys)
    {
        if (n->nodePtr == -1)
        {
            unpinBTNode(handle->tree, btsmd->pn);
            btsmd->pn= -1;
            btsmd->curPos= -1;
            btsmd->curNode= NULL;

            RETURN(RC_IM_NO_MORE_ENTRIES);
        }

        pn= n->nodePtr;
        unpinBTNode(handle->tree, btsmd->pn);

        btsmd->pn= pn;
        btsmd->curPos= 0;
        n= btsmd->curNode= getPinnedBTNode(handle->tree, pn);
    }

    // Read the RID
    el= &n->el;
    *result= *((RID*) &el[btsmd->curPos].ptr);
    btsmd->curPos++;

    return(RC_OK);
}

RC closeTreeScan (BT_ScanHandle *handle)
{
    RC rc;
    BT_ScanMgmtData *btsmd= handle->mgmtData;

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(rc);

    // Close the scan and unpin active page in buffer
    if (btsmd->pn != -1)
    {
        unpinBTNode(handle->tree, btsmd->pn);
        btsmd->pn= -1;
    }

    free(handle->mgmtData);
    free(handle);

    return(RC_OK);
}

// debug and test functions
static char* printTreeOut(BTreeHandle *tree, char *outbuf)
{
    BT_MgmtData *btmd = (BT_MgmtData*) tree->mgmtData;
    BT_Node *n,*nextNode;
    PageNumber pn;
    BT_NodeElement *el;
    int i, cnt=0,node = 0;
    n= getPinnedBTNode(tree, (PageNumber)btmd->rootPage);

    typedef struct Node
    {
       int nodeNumber;
       struct Node *next;
    } Node;

    // Print node details
    el= &n->el;

    //create table of node location on disk to in-order node location in tree
    int table[btmd->nodeCount];
    
    //will make a stack and a queue
    Node *sn,*head;
    head= NULL;
    table[node++] = btmd->rootPage;
    //store pointed to nodes in stack
    if (n->nodePtr>=0  )
    {
         sn = malloc(sizeof(Node));
         sn->nodeNumber = (int)n->nodePtr;
         sn->next = head;
         head = sn;
    }
    for(cnt=n->numKeys-1; cnt>=0; cnt--)
    {
         sn = malloc(sizeof(Node));
         sn->nodeNumber = (int)el[cnt].ptr;
         sn->next = head;
         head = sn;
    }
    unpinBTNode(tree, btmd->rootPage);
    while (head != NULL)
    {
         nextNode = getPinnedBTNode(tree, (PageNumber) head->nodeNumber);
         pn = head->nodeNumber;
         if (nextNode->leaf)
         {
             //pop stack
             table[node++] = (int)(head->nodeNumber);
             sn = head;
             head = head->next;
	     free(sn);
             unpinBTNode(tree, pn);
          }
          else
          {
	     //pop off node
             table[node++] = head->nodeNumber;
             sn = head;
	     free(sn);
             head = head->next;
 
    	     if (nextNode->nodePtr>=0)
	     {
	         sn = malloc(sizeof(Node));
                 sn->nodeNumber = (int)nextNode->nodePtr;
                 sn->next = head;
                 head = sn;
             }
	     for(cnt=nextNode->numKeys-1; cnt>=0; cnt--)
             {
                 sn = malloc(sizeof(Node));
                 sn->nodeNumber = (int)el[cnt].ptr;
                 sn->next = head;
                 head = sn;
	     }
	     unpinBTNode(tree, pn);
          }
      }

    for (node = 0; node < btmd->nodeCount; node++)
    {
       n= getPinnedBTNode(tree, (PageNumber)table[node]);
       outbuf+= sprintf(outbuf,"(%d)[", node);
       el= &n->el;
       for(cnt=0; cnt<n->numKeys; cnt++)
       {
          if(cnt) outbuf+= sprintf(outbuf,",");
          if (n->leaf)
          {
             RID r= *((RID*) &el[cnt].ptr);
             outbuf+= sprintf(outbuf,"%d.%d", r.page, r.slot);
          }
          else
          {
             for (i =0; i < btmd->nodeCount; i++)
             {
		if (table[i] == el[cnt].ptr)
                   break;
	     }
             outbuf+= sprintf(outbuf,"%d", i);
          }
          outbuf+= sprintf(outbuf,",%s", serializeValue(&el[cnt].key));
       }
      if (n->nodePtr>=0)
      {
             for (i =0; i < btmd->nodeCount; i++)
             {
		if (table[i] == n->nodePtr)
                   break;
	     }
             if (i == btmd->nodeCount)
                 ;
             else
                outbuf+= sprintf(outbuf,",%d", i);

      }
      outbuf+= sprintf(outbuf,"]\n");
      unpinBTNode(tree, table[node]);
   }
    return(outbuf);
}

extern char *printTree (BTreeHandle *tree)
{ 
    RC rc;
    char *outbuf= (char*) malloc(8192*2); // Can be increased if required.
    memset(outbuf, 0, 8192*2);

    // Initialized ?
    if ((rc=isStorageManagerInitialized()) != RC_OK)
        return(NULL);

    printTreeOut(tree, outbuf);
    return(outbuf);
}
