#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "briefJson.h"

#define BUFFER_SIZE 1024

const static wchar_t espsrc[] = L"\b\t\n\f\r\"'";
const static wchar_t *const espdes[] = { L"\\b",L"\\t",L"\\n",L"\\f",L"\\r",L"\\\"",L"\\'" };
const static wchar_t espuni[] = L"\\u";
const static size_t sz_esp = sizeof(espdes) / sizeof(void *);

typedef struct
{
	json_object data;
	wchar_t *json;
	wchar_t *pos;
	wchar_t *message;
}parse_engine;

typedef struct string_node
{
	size_t size;
	struct string_node *next;
	wchar_t string[BUFFER_SIZE];
}string_node;

typedef struct
{
	size_t size;
	string_node *last;
	string_node first;
}string_buffer;

static void buffer_append(string_buffer *buffer, const wchar_t* string, size_t length)
{
	if (!buffer->last)
		buffer->last = &buffer->first;
	const wchar_t *pos = string;
	while (length)
	{
		size_t copysize = length;
		string_node *copynode = buffer->last;
		if (copynode->size + length > BUFFER_SIZE)
		{
			copysize = BUFFER_SIZE - copynode->size;
			string_node *node = (string_node *)malloc(sizeof(string_node));
			node->next = 0;
			node->size = 0;
			copynode->next = node;
			buffer->last = node;
		}
		wcsncpy(copynode->string + copynode->size, pos, copysize);
		pos += copysize;
		length -= copysize;
		buffer->size += copysize;
		copynode->size += copysize;
	}
}

static void buffer_addchar(string_buffer *buffer, const wchar_t ch)
{
	if (!buffer->last)
		buffer->last = &buffer->first;
	if (buffer->last->size == BUFFER_SIZE)
	{
		buffer->last->next = (string_node *)malloc(sizeof(string_node));
		buffer->last = buffer->last->next;
		buffer->last->next = 0;
		buffer->last->size = 0;
	}
	buffer->last->string[buffer->last->size++] = ch;
	++buffer->size;
}

static wchar_t *buffer_tostr(string_buffer *buffer)
{
	wchar_t *string = (wchar_t *)malloc(sizeof(wchar_t)*(buffer->size + 1));
	wchar_t *pos = string;
	string[buffer->size] = 0;
	string_node *first = &buffer->first;
	wcsncpy(pos, first->string, first->size);
	pos += first->size;
	while (first->next)
	{
		string_node *node = first->next;
		first->next = node->next;
		wcsncpy(pos, node->string, node->size);
		pos += node->size;
		free(node);
	}
	return string;
}

static void buffer_free(string_buffer *buffer)
{
	string_node *first = &buffer->first;
	while (first->next)
	{
		string_node *node = first->next;
		first->next = node->next;
		free(node);
	}
}

static void string_escape(string_buffer *sb, wchar_t string[], size_t length)
{
	for (size_t i = 0; i < length; ++i)
	{
		const wchar_t ch = string[i];
		const wchar_t *posesp = wcschr(espsrc, ch);
		if (posesp)
		{
			const wchar_t *str = espdes[posesp - espsrc];
			buffer_append(sb, str, wcslen(str));
		}
		else
			buffer_addchar(sb, ch);
	}
}

static wchar_t* string_revesp(wchar_t string[], wchar_t *end)
{
	wchar_t *pos = string;
	string_buffer sb = { 0 };
	while (pos < end)
	{
		if (!wcsncmp(pos, espuni, 2))
		{
			if (end - pos < 6) {
				buffer_free(&sb);
				return 0;
			}
			pos += 2;
			wchar_t num = 0;
			for (int i = 0; i < 4; ++i)
			{
				wchar_t tmp = *pos++;
				if (tmp >= '0'&&tmp <= '9')
					tmp = tmp - '0';
				else if (tmp >= 'A'&&tmp <= 'F')
					tmp = tmp - ('A' - 10);
				else if (tmp >= 'a'&&tmp <= 'f')
					tmp = tmp - ('a' - 10);
				num = num << 4 | tmp;
			}
			buffer_addchar(&sb, num);
			continue;
		}
		size_t i = 0;
		for (; i < sz_esp; ++i)
		{
			size_t len = wcslen(espdes[i]);
			if (!wcsncmp(pos, espdes[i], len))
			{
				buffer_addchar(&sb, espsrc[i]);
				pos += len;
				break;
			}
		}
		if (i == sz_esp)
			buffer_addchar(&sb, *pos++);
	}
	return buffer_tostr(&sb);
}

static json_object* insert_item(json_object *list, wchar_t *key)
{
	json_object *item;
	size_t len = 0;
	if (key)
	{
		len = wcslen(key);
		item = (json_object *)malloc(sizeof(json_object) + sizeof(wchar_t)*len);
		wcsncpy(item->key, key, len);
	}
	else item = (json_object *)malloc(sizeof(json_object));
	item->type = NONE;
	item->key[len] = 0;
	item->next = 0;
	if (!list->value.item) list->value.item = item;
	else {
		static json_object *head = 0;
		static json_object *rear = 0;
		if (!rear || list != head)
		{
			head = list;
			for (json_object *i = list->value.item;; i = i->next)
				if (!i->next)
				{
					rear = i;
					break;
				}
		}
		rear->next = item;
		rear = item;
	}
	return item;
}

static wchar_t next_token(parse_engine* engine) {
	wchar_t ch;
	while ((ch = *engine->pos++) <= ' '&&ch>0);
	return *(engine->pos - 1);
}

static int parsing(parse_engine* engine, json_object *pos_parse)
{
	wchar_t c = next_token(engine);
	switch (c)
	{
	case 0:
		engine->message = (wchar_t *)L"Unexpected end";
		return 1;

	case '[':
	{
		pos_parse->type = ARRAY;
		pos_parse->value.item = 0;
		if (next_token(engine) == ']') return 0;
		--engine->pos;
		while (1)
		{
			json_object *item = insert_item(pos_parse, 0);
			if (next_token(engine) == ',')
			{
				--engine->pos;
			}
			else
			{
				--engine->pos;
				if (parsing(engine, item))
					return 1;
			}
			switch (next_token(engine))
			{
			case ',':
				if (next_token(engine) == ']')
					return 0;
				--engine->pos;
				break;
			case ']':
				return 0;
			default:
				engine->message = (wchar_t *)L":Expected a ',' or ']'";
				return 1;
			}
		}
	}

	case '{':
	{
		pos_parse->type = TABLE;
		pos_parse->value.item = 0;
		if (next_token(engine) == '}') return 0;
		--engine->pos;
		while (1)
		{
			json_object key;
			if (parsing(engine, &key) || key.type != TEXT)
			{
				json_object_free(&key);
				engine->message = (wchar_t *)L"Illegal key of pair";
				return 1;
			}
			if (next_token(engine) != ':')
			{
				engine->message = (wchar_t *)L"Expected a ':'";
				return 1;
			}
			json_object* item = insert_item(pos_parse, key.value.text);
			json_object_free(&key);
			if (parsing(engine, item))
			{
				return 1;
			}
			switch (next_token(engine))
			{
			case ';':
			case ',':
				if (next_token(engine) == '}')
					return 0;
				--engine->pos;
				break;
			case '}':
				return 0;
			default:
				engine->message = (wchar_t *)L"Expected a ',' or '}'";
				return 1;
			}
		}
	}

	case '\'':
	case '"':
	{
		wchar_t *start = engine->pos;
		while (*engine->pos != c)
		{
			engine->pos += *engine->pos == '\\';
			if (!*engine->pos++) {
				engine->message = (wchar_t *)L"Unterminated string";
				return 1;
			}
		}
		pos_parse->type = TEXT;
		pos_parse->value.text = string_revesp(start, engine->pos++);
		return !pos_parse->value.text;
	}
	}
	wchar_t *start = engine->pos - 1;
	while (c >= ' ') {
		if (strchr(",:]}/\"[{;=#", c))
			break;
		c = *engine->pos++;
	}
	wchar_t *string = string_revesp(start, --engine->pos);
	if (!string) return 1;
	if (!wcscmp(string, L"TRUE") || !wcscmp(string, L"true"))
	{
		pos_parse->type = BOOLEAN;
		pos_parse->value.boolean = true;
	}
	else if (!wcscmp(string, L"FALSE") || !wcscmp(string, L"false"))
	{
		pos_parse->type = BOOLEAN;
		pos_parse->value.boolean = false;
	}
	else if (!wcscmp(string, L"NULL") || !wcscmp(string, L"null"))
	{
		pos_parse->type = NONE;
	}
	else
	{
		pos_parse->type = (wcschr(string, L'.') || wcschr(string, L'e') || wcschr(string, L'E')) ? DECIMAL : INTEGER;
		const wchar_t *format = pos_parse->type == INTEGER ? L"%lld" : L"%lf";
		if (!swscanf(string, format, &pos_parse->value))
		{
			pos_parse->type = TEXT;
			pos_parse->value.text = string;
			return 0;
		}
	}
	free(string);
	return 0;
}

static void object_to_string(json_object *data, string_buffer *sb)
{
	switch (data->type) {
	case NONE:
	{
		buffer_append(sb, L"null", 4);
		break;
	}
	case INTEGER:
	case DECIMAL:
	{
		wchar_t buffer[32] = { 0 };
		if (data->type == INTEGER)
			swprintf(buffer, sizeof(buffer), L"%lld", data->value.integer);
		else
			swprintf(buffer, sizeof(buffer), L"%lf", data->value.decimal);
		buffer_append(sb, buffer, wcslen(buffer));
		break;
	}
	case BOOLEAN:
	{
		if (data->value.boolean)
			buffer_append(sb, L"true", 4);
		else
			buffer_append(sb, L"false", 5);
		break;
	}
	case TEXT:
	{
		buffer_addchar(sb, L'"');
		string_escape(sb, data->value.text, wcslen(data->value.text));
		buffer_addchar(sb, L'"');
		break;
	}
	case TABLE:
	{
		json_object *item = data->value.item;
		int index = 0;
		while (item)
		{
			if (index) buffer_addchar(sb, L',');
			else buffer_addchar(sb, L'{');
			json_object key;
			key.type = TEXT;
			key.value.text = item->key;
			object_to_string(&key, sb);
			buffer_addchar(sb, L':');
			object_to_string(item, sb);
			item = item->next;
			++index;
		}
		buffer_addchar(sb, L'}');
		break;
	}
	case ARRAY:
	{
		json_object *item = data->value.item;
		int index = 0;
		while (item)
		{
			buffer_addchar(sb, index ? L',' : L'[');
			object_to_string(item, sb);
			item = item->next;
			++index;
		}
		buffer_addchar(sb, L']');
		break;
	}
	}
}

json_object json_parse(wchar_t json[], wchar_t **message, long* error_pos)
{
	parse_engine result;
	result.data.type = NONE;
	result.pos = json;
	result.json = json;
	if (parsing(&result, &result.data))
	{
		if (message)		*message = result.message;
		json_object_free(&result.data);
		if (error_pos)		*error_pos = result.pos - result.json;
		json_object null_item;
		null_item.type = NONE;
		return null_item;
	}
	if (message)	*message = (wchar_t *)L"SUCCEED";
	if (error_pos) *error_pos = 0;
	return result.data;
}

wchar_t *json_serialize(json_object *data)
{
	string_buffer head = { 0 };
	object_to_string(data, &head);
	wchar_t *string = buffer_tostr(&head);
	return string;
}

void json_object_free(json_object *data)
{
	if (data->type == ARRAY || data->type == TABLE)
		while (data->value.item)
		{
			json_object *item = data->value.item;
			data->value.item = item->next;
			json_object_free(item);
			free(item);
		}
	else if (data->type == TEXT)
		free(data->value.text);
}

void json_text_free(wchar_t json[])
{
	free(json);
}
