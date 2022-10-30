/*==========================================================*/
/*							RB-TREE							*/
/*==========================================================*/
#pragma once
#include "core.h"

typedef struct rbnode_s {
	size_t data;
	/* Left and right children and parent pointer, 
	 * parent pointer contains a tagged color bit so it cannot be at 
	 * odd adresses
	 */
	struct rbnode_s *left, *right, *parent;
} rbnode_t;

typedef struct {
	rbnode_t* root;
	size_t size;
} rbtree_t;

static rbnode_t RBNIL = {
	.data = 0U,
	.left = &RBNIL,
	.right = &RBNIL,
	.parent = &RBNIL,
};
/* Private function declaration: */
static inline void rb_rotate_left(rbnode_t** root, rbnode_t* x);
static inline void rb_rotate_right(rbnode_t** root, rbnode_t* y);
static inline void rb_insert_fixup(rbnode_t** root, rbnode_t* z);
static inline void rb_delete_fixup(rbnode_t** r, rbnode_t* x);
static inline void rb_transplant(rbnode_t** root, rbnode_t* x, rbnode_t* y);

#define RB_NODE(_x) ((rbnode_t*)((size_t)_x & ~1))
#define RB_COLORBIT(_x) ((size_t) (_x)->parent & 1)
#define RB_RED 1U
#define RB_BLACK 0U
#define RB_SETRED(_x) \
(_x)->parent = (rbnode_t*)((size_t)(_x)->parent | RB_RED)
#define RB_SETBLACK(_x) \
(_x)->parent = RB_NODE((_x)->parent)
#define RB_SETCOLOR(_x, _y, _z) \
 (_x)->parent = (rbnode_t*)((size_t)(_y) | (_z));


inline static rbtree_t rb_init() 
{ 
	return (rbtree_t) { .root = &RBNIL }; 
}

inline bool rb_empty(rbtree_t* t) 
{ 
	return t->root == &RBNIL;
}

inline rbnode_t* rb_min(rbnode_t* x)
{
	for (; x->left != &RBNIL; x = x->left);
	return x;
}

inline rbnode_t* rb_max(rbnode_t* x)
{
	for (; x->right != &RBNIL; x = x->right);
	return x;
}

inline void rb_insert(rbtree_t* t, rbnode_t* z) 
{
	TC_ASSERT(RB_NODE(z) == z, 
		"[RB-Tree]: Make sure the new node is on an even address");
	rbnode_t* y = &RBNIL;
	rbnode_t* x = t->root;
	while (x != &RBNIL) {
		y = x;
		if (z->data < x->data)
			x = x->left;
		else x = x->right;
	}
	z->left = z->right = &RBNIL;
	RB_SETCOLOR(z, y, 1U);
	if (y == &RBNIL)
		t->root = z;
	else if (z->data < y->data)
		y->left = z;
	else y->right = z;
	rb_insert_fixup(&t->root, z);
	t->size++;
}

inline void rb_remove(rbtree_t* t, rbnode_t* z) 
{
	rbnode_t* y, * x;
	y = z;
	size_t c = RB_COLORBIT(y);
	if (z->left == &RBNIL) {
		x = z->right;
		rb_transplant(&t->root, z, z->right);
	}
	else if (z->right == &RBNIL) {
		x = z->left;
		rb_transplant(&t->root, z, z->left);
	}
	else {
		y = rb_min(z->right);
		c = RB_COLORBIT(y);
		x = y->right;
		if (RB_NODE(y->parent) == z) {
			RB_SETCOLOR(x, y, RB_COLORBIT(x));
		}
		else {
			rb_transplant(&t->root, y, y->right);
			y->right = z->right;
			RB_SETCOLOR(y->right, y, RB_COLORBIT(y->right));
		}
		rb_transplant(&t->root, z, y);
		y->left = z->left;
		RB_SETCOLOR(y->left, y, RB_COLORBIT(y->left));
		RB_SETCOLOR(y, RB_NODE(y->parent), RB_COLORBIT(z));
	}
	if (c == RB_BLACK) rb_delete_fixup(&t->root, x);
	t->size--;
}

inline rbnode_t* rb_find(rbtree_t* t, size_t key) 
{
	rbnode_t* x = t->root;
	while (x != &RBNIL) {
		if (x->data == key) return x;
		else if (x->data > key)
			x = x->left;
		else
			x = x->right;
	}
	return NULL;
}

inline rbnode_t* rb_begin(rbtree_t* t)
{
	rbnode_t* it = NULL;
	rbnode_t* x = t->root;
	while (x->left != &RBNIL || x->right != &RBNIL) {
		while (x->left != &RBNIL)
			x = x->left;
		if (x->right != &RBNIL) x = x->right;
	}
	it = x;
	return it;
}

inline rbnode_t* rb_end(rbtree_t* t) 
{
	return &RBNIL; 
}

inline size_t rb_size(rbtree_t* t) 
{ 
	return t->size; 
}

inline rbnode_t* rb_next(rbnode_t** it) 
{
	rbnode_t* x = (*it);
	if ((*it)->right != &RBNIL) {
		(*it) = (*it)->right;
		while ((*it)->left != &RBNIL)
			(*it) = (*it)->left;
		return x;
	}
	rbnode_t* p;
	for (;;) {
		p = (*it);
		(*it) = RB_NODE((*it)->parent);
		if ((*it) == &RBNIL) break;
		else if ((*it)->left == p) break;
	}
	return x;
}

inline rbnode_t* rb_prev(rbnode_t** it) 
{
	rbnode_t* x = (*it);
	if ((*it)->left != &RBNIL) {
		(*it) = (*it)->left;
		while ((*it)->right != &RBNIL)
			(*it) = (*it)->right;
		return x;
	}
	rbnode_t* p;
	for (;;) {
		p = (*it);
		(*it) = RB_NODE((*it)->parent);
		if ((*it) == &RBNIL) break;
		if ((*it)->right == p) break;
	}
	return x;
}

inline rbnode_t* rb_lower_bound(rbtree_t* t, size_t key) 
{
	rbnode_t* x = t->root, * p = &RBNIL;
	rbnode_t* it = p;
	for (;;) {
		if (x == &RBNIL) {
			it = p;
			if (!(p == &RBNIL || key <= p->data))
				rb_next(&it);
			return it;
		}
		p = x;
		if (key > x->data) x = x->right;
		else x = x->left;
	}
	return it;
}

inline rbnode_t* rb_upper_bound(rbtree_t* t, size_t key) 
{
	rbnode_t* it = rb_lower_bound(t, key);
	while (it != &RBNIL && it->data == key)
		rb_next(&it);
	return it;
}


static
inline void rb_rotate_left(rbnode_t** root, rbnode_t* x)
{
	rbnode_t* y = x->right;
	x->right = y->left;
	if (y->left != &RBNIL)
		RB_SETCOLOR(y->left, x, RB_COLORBIT(y->left));
	RB_SETCOLOR(y, RB_NODE(x->parent), RB_COLORBIT(y));
	if (RB_NODE(x->parent) == &RBNIL)
		(*root) = y;
	else if (x == RB_NODE(x->parent)->left)
		RB_NODE(x->parent)->left = y;
	else
		RB_NODE(x->parent)->right = y;
	y->left = x;
	RB_SETCOLOR(x, y, RB_COLORBIT(x));
}

static
inline void rb_rotate_right(rbnode_t** root, rbnode_t* y)
{
	rbnode_t* x = y->left;
	y->left = x->right;
	if (x->right != &RBNIL)
		RB_SETCOLOR(x->right, y, RB_COLORBIT(x->right));
	RB_SETCOLOR(x, RB_NODE(y->parent), RB_COLORBIT(x));
	if (RB_NODE(x->parent) == &RBNIL)
		(*root) = x;
	else if (y == RB_NODE(y->parent)->right)
		RB_NODE(y->parent)->right = x;
	else
		RB_NODE(y->parent)->left = x;
	x->right = y;
	RB_SETCOLOR(y, x, RB_COLORBIT(y));
}

static
inline void rb_insert_fixup(rbnode_t** root, rbnode_t* z)
{
	rbnode_t* y;
	rbnode_t* p = RB_NODE(z->parent);
	while (z != (*root) && RB_COLORBIT(p) == RB_RED) {
		p = RB_NODE(z->parent);
		if (p == RB_NODE(p->parent)->left) {
			y = RB_NODE(p->parent)->right;
			if (RB_COLORBIT(y) == RB_RED) {
				RB_SETBLACK(p);
				RB_SETBLACK(y);
				p = RB_NODE(p->parent);
				RB_SETRED(p);
				z = p;
			}
			else {
				if (z == p->right) {
					z = p;
					rb_rotate_left(root, z);
				}
				p = RB_NODE(z->parent);
				RB_SETBLACK(p);
				p = RB_NODE(p->parent);
				RB_SETRED(p);
				rb_rotate_right(root, p);
			}
		}
		else {
			y = RB_NODE(RB_NODE(z->parent)->parent)->left;
			if (RB_COLORBIT(y) == RB_RED) {
				RB_SETBLACK(p);
				RB_SETBLACK(y);
				p = RB_NODE(p->parent);
				RB_SETRED(p);
				z = p;
			}
			else {
				if (z == p->left) {
					z = p;
					rb_rotate_right(root, z);
				}
				p = RB_NODE(z->parent);
				RB_SETBLACK(p);
				p = RB_NODE(p->parent);
				RB_SETRED(p);
				rb_rotate_left(root, p);
			}
		}
		p = RB_NODE(z->parent);
	}
	RB_SETBLACK(*root);
}

static
inline void rb_delete_fixup(rbnode_t** r, rbnode_t* x)
{
	while (x != *r && RB_COLORBIT(RB_NODE(x->parent)) == 0) {
		rbnode_t* y, * p;
		if (x == RB_NODE(x->parent)->left) {
			y = RB_NODE(x->parent)->right;
			if (RB_COLORBIT(y)) {
				RB_SETBLACK(y);
				p = RB_NODE(x->parent);
				RB_SETRED(p);
				rb_rotate_left(r, p);
				y = RB_NODE(x->parent)->right;
			}
			if (RB_COLORBIT(y->left) == RB_BLACK &&
				RB_COLORBIT(y->right) == RB_BLACK) {
				RB_SETRED(y);
				x = RB_NODE(x->parent);
			}
			else {
				if (RB_COLORBIT(y->right) == RB_BLACK) {
					RB_SETBLACK(y->left);
					RB_SETRED(y->parent);
					rb_rotate_right(r, y);
					y = RB_NODE(y->parent)->right;
				}
				RB_SETCOLOR(y, RB_NODE(y->parent), RB_COLORBIT(RB_NODE(x->parent)));
				p = RB_NODE(x->parent);
				RB_SETBLACK(p);
				RB_SETBLACK(y->right);
				rb_rotate_left(r, RB_NODE(x->parent));
				x = (*r);
			}
		}
		else {
			y = RB_NODE(x->parent)->left;
			if (RB_COLORBIT(y)) {
				RB_SETBLACK(y);
				p = RB_NODE(x->parent);
				RB_SETRED(p);
				rb_rotate_right(r, p);
				y = p->left;
			}
			if (RB_COLORBIT(y->right) == RB_BLACK &&
				RB_COLORBIT(y->left) == RB_BLACK) {
				RB_SETRED(y);
				x = RB_NODE(x->parent);
			}
			else {
				if (RB_COLORBIT(y->left) == RB_BLACK) {
					RB_SETBLACK(y->right);
					RB_SETRED(y);
					rb_rotate_left(r, y);
					y = RB_NODE(y->parent)->left;
				}
				RB_SETCOLOR(y, RB_NODE(y->parent), RB_COLORBIT(RB_NODE(x->parent)));
				p = x->parent;
				RB_SETBLACK(p);
				RB_SETBLACK(y->left);
				rb_rotate_right(r, RB_NODE(x->parent));
				x = (*r);
			}
		}
	}
	RB_SETBLACK(x);
}

static
inline void rb_transplant(rbnode_t** root, rbnode_t* x, rbnode_t* y)
{
	if (RB_NODE(x->parent) == &RBNIL)
		(*root) = y;
	else if (x == RB_NODE(x->parent)->left)
		RB_NODE(x->parent)->left = y;
	else RB_NODE(x->parent)->right = y;
	RB_SETCOLOR(y, RB_NODE(x->parent), RB_COLORBIT(y));
}