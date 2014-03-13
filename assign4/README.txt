ASSIGNMENT 4 APPROACH
----------------------
Each B-Tree node is stored in a page.  We sort elements in the
BT_Node on key. When inserting a key we iterate thru
the keys in the node to see if there is a key greater than the 
key to be inserted.  If so and there is room we shift all keys 
from this point down the length of one element.  We then 
assign this element with the key and ptr to be inserted.

We follow this same methodolgy with delete.  In this case when 
we locate the key that we are searching for, we move all remaining
keys up in the BT_Node.

This method optimizes for space over CPU time.

The BT_Node struct contains a boolean value to assert whether
node is leaf or not.  The struct also store the number of keys
in the node. Also in the node, elements are stored.  Elements are
structs that contain a key and pointer value.  A nodePtr value
is stored and represents the page pointed to by the last pointer
in a BT_Node.

When a b-tree iindex is created, the first page contains meta-data 
for the index.  An integer representing where the root node of the 
tree is recorded.  When the root node is modified, this field 
is also updated.  Also fields recording number of keys as well
as the number of nodes in the tree are updated as inserts and 
deletes take place.  This meta-data also records the type of 
key used to identify records in the b-tree index and the order
of elements one BT_Node can store.  These two fields do not  
change.

The static function findElement is used often and is also called
by the user interface function findKey.  It works to find the 
element in  the b-tree that stores the key/ptr combination that 
the caller is looking for.  

The static function addKeyInNode which accepts parameters 
BTreeHandle *tree, BT_Node *node, long long ptr, Value *key, is 
used to handle inserts.  First the function findElement, which 
returns a BT_Node, is called to find the node where we want to do
the insert then with this node we call the former function.  

If inserting a key causes a node to split.  Then a static function
splitAndInsertKey is called.  This function copies the (order+1)/2
elements from the original node (referred to as left node) to a 
newly created node (referred to as right node).  This will then 
require another static function insertKeyIntoParent to insert 
the key value and ptr information to the parent of the original
node.  If this requires another split it is handled.

Deletion happens when user calls deletekey.  This will call static
functions deleteElement which will call delKeyFromNode.  When 
deleting keys, the converse may happen.  A BT_Node can 
underflow and then the static mergeElements is called.  PageNumber
references are passed.  A static function getNeighbor gets the 
PageNumber it prefers to return then left neighbor for merging.  
If not an option then it returns the right neighbor. If there are
no neighbors then it returns 0 which will indicate that this was
a single leaf.  0 won't be interpreted as a correct pn because
page 0 only stores the meta data.

Scanning works by calling openTreeScan passing in a BT_ScanHandle.  
Then the client can call nextEntry on the handle.  Client checks 
for no more keys then calls closeTreeScan.

TESTING
-------
All test pass.
Fixed all valgrind issues.

Pending cases to handle
-----------------------
1) Once a BT_Node becomes free after all elements are deleted
we can reuse it, but this is not handled as part of submission.

2) Non-leaf BT_Node distribution is not handled.


