#pragma once

#include <json-c/json.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    JSON_NODE_STRING,
    JSON_NODE_JSON_C,
    JSON_NODE_MEMBER, // Internal node for object key-value pairs
    JSON_NODE_LIST,   // Internal node for list of elements or members
} json_node_type_t;

typedef struct json_node_t json_node_t;

typedef struct
{
    json_node_t ** items; // Pointer to array of json_node_t pointers
    size_t count;         // Number of items currently in the list
    size_t capacity;      // Total allocated capacity of the items array
} json_list_t;

typedef struct
{
    char * key;
    json_node_t * value;
} json_member_t;

struct json_node_t
{
    json_node_type_t type;
    union
    {
        json_list_t list;     // Used by OBJECT and ARRAY
        char * string;        // Used by STRING
        json_member_t member; // Used by MEMBER
    };
    struct json_object * obj; // Used to store json-c objects
};

/* Function to free a JSON AST node and its children */
void json_node_free(void * node, void * user_data);
