#include <solution.h>
#include <stdlib.h>
#include <stdio.h>

#include <assert.h>

struct btree_node;

struct btree
{
	struct btree_node* root;
	unsigned int L;
};

struct btree_node {
	struct btree* btree;
	int* elems;
	struct btree_node** ptrs;
	struct btree_node* parent;
	struct btree_node* right;
	struct btree_node* left;
	size_t size;
	size_t parent_pos;
};

void print_node(struct btree_node* node) {
	if (node == NULL) {
		printf("NULL\n"); fflush(stdout);
		return;
	}
	printf("print %p, %ld elems: ", node, node->size);
	for (size_t i = 0; i < node->size; ++i) {
		printf("%d ", node->elems[i]);
	}
	printf("\n"); fflush(stdout);
	if (node->ptrs[0]) {
		for (size_t i = 0; i < node->size; ++i) {
			printf("go to %ld\n", i); fflush(stdout);
			print_node(node->ptrs[i]);
			printf("return\n"); fflush(stdout);
		}
	} else {
		printf("list\n"); fflush(stdout);
	}
}

struct btree_node* node_alloc(struct btree* tree) {
	assert(tree != NULL);
	struct btree_node* node = malloc(sizeof(struct btree_node));
	node->btree = tree;
	node->size = 0;
	node->parent_pos = 0;
	node->elems = malloc(sizeof(int) * (2 * tree->L + 1));
	node->ptrs = malloc(sizeof(struct btree_node*) * (2 * tree->L + 1));
	node->parent = NULL;
	node->right = NULL;
	node->left = NULL;
	return node;
}

void node_free(struct btree_node* node) {
	if (node == NULL) {
		return;
	}
	for (size_t i = 0; i < node->size; ++i) {
		node_free(node->ptrs[i]);
	}
	free(node->ptrs);
	free(node->elems);
	free(node);
}


struct btree_iter
{
	struct btree_node* node;
	size_t num;
};


size_t node_find_simple(struct btree_node* node, int x) {
	assert(node != NULL);
	if (node->size == 0) {
		return 0;
	}
	size_t result = node->size;
	for (size_t i = 0; i < node->size; ++i) {
		if (x <= node->elems[i]) {
			result = i;
			break;
		}
	}
	if (node->ptrs[0] && result > 0) {
		if (result >= node->size || x < node->elems[result]) {
			--result;
		}
	}
	return result;
}

struct btree_iter node_find(struct btree_node* node, int x) {
	struct btree_iter result;
	result.node = node;
	result.num = 0;
	if (node == NULL) {
		return result;
	}
	result.num = node_find_simple(result.node, x);
	while (result.num < result.node->size && result.node->ptrs[result.num] != NULL) {
		result.node = result.node->ptrs[result.num];
		result.num = node_find_simple(result.node, x);
	}
	return result;
}

void push_first(struct btree_node* node) {
	if (node == NULL || node->parent == NULL) {
		return;
	}
	node->parent->elems[node->parent_pos] = node->elems[0];
	if (node->parent_pos == 0) {
		push_first(node->parent);
	}
}


void make_neighbours(struct btree_node* left, struct btree_node* right) {
	if (left) {
		left->right = right;
	}
	if (right) {
		right->left = left;
	}
}

struct btree_node* find_right(struct btree_node* node) {
	if (node == NULL || node->parent == NULL) {
		return NULL;
	}
	const struct btree_node* const parent = node->parent;
	if (node->parent_pos < parent->size - 1) {
		return parent->ptrs[node->parent_pos + 1];
	}
	if (parent->right == NULL) {
		return NULL;
	}
	return parent->right->ptrs[0];
}

void set_right(struct btree_node* node) {
	make_neighbours(node, find_right(node));
}

struct btree_node* find_left(struct btree_node* node) {
	if (node == NULL || node->parent == NULL) {
		return NULL;
	}
	const struct btree_node* const parent = node->parent;
	if (node->parent_pos > 0) {
		return parent->ptrs[node->parent_pos - 1];
	}
	if (parent->left == NULL) {
		return NULL;
	}
	return parent->left->ptrs[parent->left->size - 1];
}

void set_left(struct btree_node* node) {
	make_neighbours(find_left(node), node);
}


void node_insert_simple(struct btree_iter* iter, int x, struct btree_node* ptr) {
	if (iter == NULL) {
		return;
	}
	for (size_t i = iter->node->size; i > iter->num; --i) {
		iter->node->elems[i] = iter->node->elems[i - 1];
		iter->node->ptrs[i] = iter->node->ptrs[i - 1];
		if (iter->node->ptrs[i]) {
			// iter->node->ptrs[i]->parent = iter->node;
			iter->node->ptrs[i]->parent_pos = i;
		}
	}
	iter->node->elems[iter->num] = x;
	iter->node->ptrs[iter->num] = ptr;
	++iter->node->size;
	if (ptr) {
		ptr->parent_pos = iter->num;
		ptr->parent = iter->node;
		set_left(ptr);
		set_right(ptr);
	}
	if (iter->num == 0) {
		push_first(iter->node);
	}
}

void node_delete_simple(struct btree_iter* iter) {
	if (iter == NULL || iter->node == NULL) {
		return;
	}
	// printf("delete %ld from %p\n", iter->num, iter->node);
	node_free(iter->node->ptrs[iter->num]);
	iter->node->ptrs[iter->num] = NULL;
	for (size_t i = iter->num; i < iter->node->size - 1; ++i) {
		iter->node->elems[i] = iter->node->elems[i + 1];
		iter->node->ptrs[i] = iter->node->ptrs[i + 1];
		if (iter->node->ptrs[i]) {
			// iter->node->ptrs[i]->parent = iter->node;
			iter->node->ptrs[i]->parent_pos = i;
		}
	}
	--(iter->node->size);
	if (iter->node->size == 0) {
		return;
	}
	if (iter->num < iter->node->size) {
		set_left(iter->node->ptrs[iter->num]);
	} else {
		set_right(iter->node->ptrs[iter->num - 1]);
	}
	if (iter->num == 0) {
		push_first(iter->node);
	}
}

void fix_overflow(struct btree_node* node) {
	if (node == NULL) {
		return;
	}
	if (node->size <= 2 * node->btree->L) {
		return;
	}
	assert(node->size == 2 * node->btree->L + 1);
	struct btree_node* right = node_alloc(node->btree);
	right->btree = node->btree;
	right->size = node->btree->L;
	right->parent = node->parent;
	make_neighbours(right, node->right);
	make_neighbours(node, right);
	for (size_t i = 0; i < node->btree->L; ++i) {
		right->elems[i] = node->elems[node->btree->L + 1 + i];
		right->ptrs[i] = node->ptrs[node->btree->L + 1 + i];
		if (right->ptrs[i]) {
			right->ptrs[i]->parent = right;
			right->ptrs[i]->parent_pos = i;
		}
		node->ptrs[node->btree->L + 1 + i] = NULL;
	}
	node->size = node->btree->L + 1;
	struct btree_iter pos;
	if (node->parent != NULL) {
		assert(node != node->btree->root);
		pos.node = node->parent;
		pos.num = node->parent_pos + 1;
		node_insert_simple(&pos, right->elems[0], right);
		fix_overflow(node->parent);
		return;
	}
	assert(node == node->btree->root);
	pos.node = node_alloc(node->btree);
	pos.num = 0;
	node_insert_simple(&pos, node->elems[0], node);
	pos.num = 1;
	node_insert_simple(&pos, right->elems[0], right);
	node->btree->root = pos.node;
}

void fix_underflow(struct btree_node* node) {
	if (node == NULL || node->size >= node->btree->L) {
		return;
	}
	if (node->parent == NULL) {
		assert(node->btree->root == node);
		if (node->size > 1 || node->size == 0 || node->ptrs[0] == NULL) { return; }
		node->btree->root = node->ptrs[0];
		node->btree->root->parent = NULL;
		node->ptrs[0] = NULL;
		node_free(node);
		return;
	}
	assert(node->size == node->btree->L - 1);
	struct btree_iter pos;
	pos.node = node;
	if (node->size == 0) {
		pos.node = node->parent;
		pos.num = node->parent_pos;
		make_neighbours(node->left, node->right);
		node_delete_simple(&pos);
		fix_underflow(pos.node);
		return;
	}
	if (node->right && node->right->size > node->btree->L) {
		pos.num = node->size;
		node_insert_simple(&pos, node->right->elems[0], node->right->ptrs[0]);
		pos.node = node->right;
		pos.num = 0;
		node->right->ptrs[0] = NULL;
		node_delete_simple(&pos);
		push_first(node->right);
		return;
	}
	if (node->left && node->left->size > node->btree->L) {
		pos.num = 0;
		node_insert_simple(&pos, node->left->elems[node->left->size - 1], node->left->ptrs[node->left->size - 1]);
		pos.node = node->left;
		pos.num = node->left->size - 1;
		pos.node->ptrs[pos.num] = NULL;
		node_delete_simple(&pos);
		push_first(node);
		return;
	}
	assert(node->parent->size > 1);
	if (node->parent_pos == node->parent->size - 1) {
		assert(node->left);
		node = node->left;
	}
	for (size_t i = 0; i < node->right->size; ++i) {
		pos.node = node;
		pos.num = node->size;
		node_insert_simple(&pos, node->right->elems[i], node->right->ptrs[i]);
		node->right->ptrs[i] = NULL;
	}
	assert(node->parent == node->right->parent);
	pos.node = node->parent;
	assert(node->parent_pos + 1 == node->right->parent_pos);
	pos.num = node->parent_pos + 1;
	make_neighbours(node, node->right->right);
	node_delete_simple(&pos);
	fix_underflow(node->parent);
}

void node_insert(struct btree_node* node, int x) {
	if (node == NULL) {
		return;
	}
	struct btree_iter pos = node_find(node, x);
	// print_node(node);
	assert(pos.node);
	if (pos.num < pos.node->size && pos.node->elems[pos.num] == x) {
		return;
	}
	node_insert_simple(&pos, x, NULL);
	// printf("-------------------------AFTER\n");
	// print_node(node);
	fix_overflow(pos.node);
}

bool node_contains(struct btree_node* node, int x) {
	struct btree_iter pos = node_find(node, x);
	return pos.node->elems[pos.num] == x;
}

void node_delete(struct btree_node* node, int x) {
	if (node == NULL) {
		return;
	}
	struct btree_iter pos = node_find(node, x);
	assert(pos.node);
	if (pos.node->elems[pos.num] != x) {
		return;
	}
	node_delete_simple(&pos);
	fix_underflow(pos.node);
	// printf("AFTER ALL\n");
	// print_node(node);
}


struct btree* btree_alloc(unsigned int L)
{
	struct btree* tree = malloc(sizeof(struct btree));
	tree->L = L;
	tree->root = node_alloc(tree);
	return tree;
}

void btree_free(struct btree *t)
{
	if (t == NULL) {
		return;
	}
	node_free(t->root);
	free(t);
}

void btree_insert(struct btree *t, int x)
{
	if (t == NULL) {
		return;
	}
	// print_node(t->root);
	node_insert(t->root, x);
	// print_node(t->root);
}

void btree_delete(struct btree *t, int x)
{
	if (t == NULL) {
		return;
	}
	// print_node(t->root);
	node_delete(t->root, x);
	// printf("-------------------------AFTER\n");
	// print_node(t->root);
}

bool btree_contains(struct btree *t, int x)
{
	if (t == NULL) {
		return false;
	}
	return node_contains(t->root, x);
}

struct btree_iter* btree_iter_start(struct btree *t)
{
	struct btree_iter* result = malloc(sizeof(struct btree_iter));
	result->node = t->root;
	result->num = 0;
	if (result->node->size == 0) {
		return result;
	}
	while (result->node->ptrs[result->num] != NULL) {
		result->node = result->node->ptrs[result->num];
	}
	return result;
}

void btree_iter_end(struct btree_iter *i)
{
	free(i);
}

bool btree_iter_next(struct btree_iter *i, int *x)
{
	if (i == NULL || i->node == NULL || i->node->size == 0) {
		return false;
	}
	*x = i->node->elems[i->num];
	++i->num;
	if (i->num < i->node->size) {
		return true;
	}
	i->node = i->node->right;
	i->num = 0;
	return true;
}
