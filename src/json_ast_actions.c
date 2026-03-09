#include "json_ast.h"

#include "json_actions.h"
#include "json_ast_actions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_LIST_INITIAL_CAPACITY 4

static bool
json_list_ensure_capacity(json_list_t * list)
{
    if (list->count < list->capacity)
    {
        return true; // Enough capacity
    }

    size_t new_capacity = list->capacity * 2;

    if (new_capacity == 0) // Handle initial zero capacity
    {
        new_capacity = JSON_LIST_INITIAL_CAPACITY;
    }

    json_node_t ** new_items = realloc(list->items, new_capacity * sizeof(json_node_t *));

    if (new_items == NULL)
    {
        return false; // Reallocation failed
    }

    list->items = new_items;
    list->capacity = new_capacity;

    return true;
}

static json_node_t *
json_node_alloc(json_node_type_t type)
{
    json_node_t * node = calloc(1, sizeof(*node));

    if (node != NULL)
    {
        node->type = type;
    }
    return node;
}

void
json_node_free(void * node_ptr, void * user_data)
{
    json_node_t * node = (json_node_t *)node_ptr;

    if (node == NULL)
    {
        return;
    }

    switch (node->type)
    {
    case JSON_NODE_STRING:
        free(node->string);
        break;

    case JSON_NODE_LIST:
    {
        // Free individual items in the dynamic array
        for (size_t i = 0; i < node->list.count; i++)
        {
            json_node_free(node->list.items[i], user_data);
        }
        // Free the array itself
        free(node->list.items);
        break;
    }

    case JSON_NODE_MEMBER:
        free(node->member.key);
        json_node_free(node->member.value, user_data);
        break;

    case JSON_NODE_JSON_C:
        /* Nothing to do. */
        break;
    }
    json_object_put(node->obj);

    free(node);
}

static void
ast_list_append(json_list_t * list, json_node_t * item)
{
    if (!json_list_ensure_capacity(list))
    {
        // Handle error: memory reallocation failed
        // For now, just return, will likely lead to further errors
        return;
    }

    list->items[list->count] = item;
    list->count++;
}

static void
free_children(void ** children, int count, void * user_data)
{
    for (size_t i = 0; i < (size_t)count; i++)
    {
        if (children[i] != NULL)
        {
            json_node_free((json_node_t *)children[i], user_data);
        }
    }
}

/* --- Semantic Action Callbacks --- */

static void
json_action_create_list_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    (void)node;
    (void)user_data;
    json_node_t * list_node = json_node_alloc(JSON_NODE_LIST);

    if (list_node == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON list node");
        return;
    }

    // Children are passed in the order they appear in the grammar
    for (size_t i = 0; i < (size_t)count; i++)
    {
        if (children[i] != NULL)
        {
            ast_list_append(&list_node->list, (json_node_t *)children[i]);
        }
    }

    epc_ast_push(ctx, list_node);
}

static void
json_action_create_member_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    (void)node;
    // member: quoted_string, colon, value

    if (count != 2)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "JSON member expected 2 children, but got %u\n", count);
        return;
    }

    json_node_t * key_node = (json_node_t *)children[0];
    json_node_t * value_node = (json_node_t *)children[1];
    json_node_t * member_node = json_node_alloc(JSON_NODE_MEMBER);

    if (member_node == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate Member node");
        return;
    }

    member_node->member.key = key_node->string;
    key_node->string = NULL;
    member_node->member.value = value_node;

    json_node_free(key_node, user_data);
    /* No need to free the value node as it is attached to the member node. */

    epc_ast_push(ctx, member_node);
}

static void
json_action_create_optional_array_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    (void)node;
    (void)user_data;

    if (count > 1 || (count == 1 && ((json_node_t *)children[0])->type != JSON_NODE_LIST))
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Expected 0 or 1 children, but got %u", count);
        return;
    }

    json_node_t * array_node = json_node_alloc(JSON_NODE_JSON_C);

    if (array_node == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON array node");
        return;
    }
    array_node->obj = json_object_new_array();

    if (count == 1)
    {
        json_node_t * list_node = (json_node_t *)children[0];

        for (size_t i = 0; i < list_node->list.count; i++)
        {
            json_object_array_add(array_node->obj, list_node->list.items[i]->obj);
            list_node->list.items[i]->obj = NULL;
        }
        json_node_free(list_node, user_data);
    }

    epc_ast_push(ctx, array_node);
}

static void
json_action_create_optional_object_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    (void)node;
    (void)user_data;

    if (count > 1 || (count == 1 && ((json_node_t *)children[0])->type != JSON_NODE_LIST))
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Expected 0 or 1 children, but got %u", count);
        return;
    }
    json_node_t * object_node = json_node_alloc(JSON_NODE_JSON_C);

    if (object_node == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON object node");
        return;
    }
    object_node->obj = json_object_new_object();

    if (count == 1)
    {
        // json_object: { , optional_members, }
        json_node_t * list_node = (json_node_t *)children[0];

        for (size_t i = 0; i < list_node->list.count; i++)
        {
            json_node_t * member = list_node->list.items[i];

            json_object_object_add(object_node->obj, member->member.key, member->member.value->obj);
            member->member.value->obj = NULL;
        }
        json_node_free(list_node, user_data);
    }

    epc_ast_push(ctx, object_node);
}

static void
json_action_create_string_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 0)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "String action expected 0 children, but got %u\n", count);
        return;
    }

    json_node_t * jnode = json_node_alloc(JSON_NODE_STRING);

    if (jnode == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON string node");
        return;
    }

    char const * content = epc_cpt_node_get_semantic_content(node);
    size_t len = epc_cpt_node_get_semantic_len(node);

    jnode->string = strndup(content, len);
    jnode->obj = json_object_new_string(jnode->string);

    epc_ast_push(ctx, jnode);
}

static void
json_action_create_null_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    (void)node;

    if (count != 0)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Null action expected 0 children, but got %u\n", count);
        return;
    }

    json_node_t * jnode = json_node_alloc(JSON_NODE_JSON_C);

    if (jnode == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON null node");
        return;
    }
    /* With json-c, a NULL value represents the null object, so there's no need to assign anything to obj. */
    epc_ast_push(ctx, jnode);
}

static void
json_action_create_integer_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 0)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Number action expected 0 children, but got %u\n", count);
        return;
    }

    json_node_t * jnode = json_node_alloc(JSON_NODE_JSON_C);

    if (jnode == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON number node");
        return;
    }

    char const * content = epc_cpt_node_get_semantic_content(node);
    size_t len = epc_cpt_node_get_semantic_len(node);
    char * endptr;
    char * buf = strndup(content, len);

    if (buf == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to duplicate number string");
        free(jnode);
        return;
    }
    jnode->obj = json_object_new_int64(strtoll(buf, &endptr, 10));
    free(buf);

    epc_ast_push(ctx, jnode);
}

static void
json_action_create_number_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 0)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Number action expected 0 children, but got %u\n", count);
        return;
    }

    json_node_t * jnode = json_node_alloc(JSON_NODE_JSON_C);

    if (jnode == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON number node");
        return;
    }

    char const * content = epc_cpt_node_get_semantic_content(node);
    size_t len = epc_cpt_node_get_semantic_len(node);
    char * endptr;
    char * buf = strndup(content, len);

    if (buf == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to duplicate number string");
        free(jnode);
        return;
    }

    jnode->obj = json_object_new_double(strtod(buf, &endptr));
    free(buf);

    epc_ast_push(ctx, jnode);
}

static void
json_action_create_boolean_action(
    epc_ast_builder_ctx_t * ctx, epc_cpt_node_t * node, void ** children, int count, void * user_data
)
{
    if (count != 0)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Boolean action expected 0 children, but got %u\n", count);
        return;
    }

    json_node_t * jnode = json_node_alloc(JSON_NODE_JSON_C);

    if (jnode == NULL)
    {
        free_children(children, count, user_data);
        epc_ast_builder_set_error(ctx, "Failed to allocate JSON boolean node");
        return;
    }

    char const * content = epc_cpt_node_get_semantic_content(node);

    jnode->obj = json_object_new_boolean(strncmp(content, "true", 4) == 0);

    epc_ast_push(ctx, jnode);
}

void
json_ast_hook_registry_init(epc_ast_hook_registry_t * registry)
{
    epc_ast_hook_registry_set_free_node(registry, json_node_free);
    epc_ast_hook_registry_set_action(
        registry, JSON_ACTION_CREATE_OPTIONAL_OBJECT_ELEMENTS, json_action_create_optional_object_action
    );
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_OBJECT_ELEMENTS, json_action_create_list_action);
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_MEMBER, json_action_create_member_action);
    epc_ast_hook_registry_set_action(
        registry, JSON_ACTION_CREATE_OPTIONAL_ARRAY_ELEMENTS, json_action_create_optional_array_action
    );
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_ARRAY_ELEMENTS, json_action_create_list_action);
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_STRING, json_action_create_string_action);
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_NULL, json_action_create_null_action);
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_INTEGER, json_action_create_integer_action);
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_NUMBER, json_action_create_number_action);
    epc_ast_hook_registry_set_action(registry, JSON_ACTION_CREATE_BOOLEAN, json_action_create_boolean_action);
}
