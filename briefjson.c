#include "briefJson.h"
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
	json_object data;
	wchar_t *json;
	unsigned pos;
	int succeed;
	wchar_t *message;
}parse_engine;

typedef struct strlist
{
	int length;
	struct strlist *next;
	union
	{
		wchar_t text[1];
		struct strlist *last;
	}v;
}strlist;

static int strlist_append(strlist *des, wchar_t *src, int length)
{
	if (!des->next)
		des->v.last = des;
	strlist *str = (strlist *)malloc(sizeof(strlist) + sizeof(wchar_t)*length);
	memcpy(str->v.text, src, sizeof(wchar_t)*length);
	str->next = 0;
	str->length = length;
	des->length += length;
	des->v.last->next = str;
	des->v.last = str;
	return des->length;
}

static void strlist_free(strlist *list)
{
	while (list->next)
	{
		strlist *tmp = list->next;
		list->next = list->next->next;
		free(tmp);
	}
}

static wchar_t *strlist_to_string(strlist *list)
{
	wchar_t *text = (wchar_t *)malloc(sizeof(wchar_t)*list->length);
	text[list->length] = 0;
	wchar_t *pos = text;
	strlist* str = list->next;
	while (str)
	{
		wcsncpy(pos, str->v.text, str->length);
		pos += str->length;
		str = str->next;
	}
	strlist_free(list);
	return text;
}

static void insert_item(json_object *list, wchar_t *key)
{
	json_object *item;
	if (key) 
	{
		int len = wcslen(key);
		item = (json_object *)malloc(sizeof(json_object) + sizeof(wchar_t)*len);
		item->key[len] = 0;
		wcsncpy(item->key, key, len);
	}else item = (json_object *)malloc(sizeof(json_object));
	item->type = NONE;
	item->next = list->value.item;
	list->value.item = item;
}

static wchar_t next_token(parse_engine* engine) {
	wchar_t ch;
	while ((ch = engine->json[engine->pos++]) <= ' '&&ch > 0);
	return engine->json[engine->pos - 1];
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

static int parsing(parse_engine* engine, json_object *pos_parse)
{
	wchar_t c = next_token(engine);
	switch (c)
	{
	case 0:
		engine->message = L"Unexpected end";
		return 1;

	case '[':
	{
		pos_parse->type = ARRAY;
		pos_parse->value.item = 0;
		if (next_token(engine) == ']') return 0;
		--engine->pos;
		while (1)
		{
			insert_item(pos_parse, 0);
			if (next_token(engine) == ',')
			{
				--engine->pos;
			}
			else 
			{
				--engine->pos;
				if (parsing(engine, pos_parse->value.item))
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
				engine->message = L":Expected a ',' or ']'";
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
				engine->message = L"Illegal key of pair";
				return 1;
			}
			if (next_token(engine) != ':')
			{
				engine->message = L"Expected a ':'";
				return 1;
			}
			insert_item(pos_parse, key.value.text);
			json_object_free(&key);
			if (parsing(engine, pos_parse->value.item))
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
				engine->message = L"Expected a ',' or '}'";
				return 1;
			}
		}
	}

	case '\'':
	case '"':
	{
		pos_parse->type = TEXT;
		pos_parse->value.text = 0;
		strlist str = { 0 };
		while (1)
		{
			wchar_t ch = engine->json[engine->pos++];
			switch (ch)
			{
			case '\n':
			case '\r':
				strlist_free(&str);
				engine->message = L"Unterminated string";
				return 1;
			case '\\':
				ch = engine->json[engine->pos++];
				switch (ch)
				{
				case 'b':
					strlist_append(&str, L"\b", 1);
					break;
				case 't':
					strlist_append(&str, L"\t", 1);
					break;
				case 'n':
					strlist_append(&str, L"\n", 1);
					break;
				case 'f':
					strlist_append(&str, L"\f", 1);
					break;
				case 'r':
					strlist_append(&str, L"\r", 1);
					break;
				case '"':
				case '\'':
				case '\\':
				case '/':
					strlist_append(&str, &ch, 1);
					break;
				case 'u': {
					wchar_t num = 0;
					for (int i = 0; i < 4; ++i)
					{
						wchar_t tmp = engine->json[engine->pos++];
						if (tmp >= '0'&&tmp <= '9')
							tmp = tmp - '0';
						else if (tmp >= 'A'&&tmp <= 'F')
							tmp = tmp - ('A' - 10);
						else if (tmp >= 'a'&&tmp <= 'f')
							tmp = tmp - ('a' - 10);
						num = num << 4 | tmp;
					}
					strlist_append(&str, &num, 1);
					break;
				}
				default:
					strlist_free(&str);
					engine->message = L"Illegal escape";
					return 1;
				}
				break;
			default:
				if (ch == c)
				{
					pos_parse->value.text = strlist_to_string(&str);
					strlist_free(&str);
					return 0;
				}
				strlist_append(&str, &ch, 1);
				break;
			}
		}
		break;
	}
	}
	const wchar_t keychar[] = { ',', ':', ']', '}', '/', '\\', '\"', '[', '{', ';', '=', '#' };
	const int keycharCount = 12;
	int length = 0;
	while (c >= ' ') {
		int i = 0;
		for (; i < keycharCount; ++i)
			if (keychar[i] == c)
				break;
		if (i != keycharCount)
			break;
		++length;
		c = engine->json[engine->pos++];
	}
	char str[32] = { 0 };
	{
		wchar_t *start = (--engine->pos) - length + engine->json;
		for (int i = 0; i < length&&i < 32; ++i) str[i] = (char)start[i];
	}
	if (!length)
	{
		pos_parse->type = TEXT;
		pos_parse->value.text = (wchar_t *)malloc(sizeof(wchar_t));
		pos_parse->value.text[0] = 0;
		return 0;
	}
	else {
		int iszero = 1;
		for (unsigned i = 0; i < strlen(str); ++i)
			if (str[i] != '0') {
				iszero = 0;
				break;
			}
		if (iszero)
		{
			pos_parse->type = INTEGER;
			pos_parse->value.integer = 0;
			return 0;
		}
		if (!strcmp(str, "TRUE") || !strcmp(str, "true"))
		{
			pos_parse->type = BOOLEAN;
			pos_parse->value.boolen = 1;
			return 0;
		}
		else if (!strcmp(str, "FALSE") || !strcmp(str, "false"))
		{
			pos_parse->type = BOOLEAN;
			pos_parse->value.boolen = 0;
			return 0;
		}
		else if (!strcmp(str, "NULL") || !strcmp(str, "null"))
		{
			pos_parse->type = NONE;
			return 0;
		}
		if (!strstr(str, "E") && !strstr(str, "e") && !strstr(str, "."))
		{
			long long value = atoll(str);
			if (value)
			{
				pos_parse->type = INTEGER;
				pos_parse->value.integer = value;
				return 0;
			}
		}
		else
		{
			double value = atof(str);
			if (value)
			{
				pos_parse->type = DECIMAL;
				pos_parse->value.decimal = value;
				return 0;
			}
		}
		engine->message = L"Unexpected end";
		return 1;
	}
}



json_object json_parse(wchar_t json[],wchar_t **message,int* error_pos)
{
	parse_engine result;
	result.data.type = NONE;
	result.pos = 0;
	result.json = json;
	if (parsing(&result, &result.data))
	{
		if(message)		*message = result.message;
		json_object_free(&result.data);
		*error_pos = result.pos;
		json_object null_item;
		null_item.type = NONE;
		return null_item;
	}
	if(message)	*message = L"SUCCEED";
	if(error_pos) *error_pos = -1;
	return result.data;
}

static void to_string(json_object *data, strlist *head)
{

	switch (data->type) {
	case NONE:
	{
		strlist_append(head, L"null", 4);
		break;
	}
	case INTEGER:
	case DECIMAL:
	{
		char tmp[32] = { 0 };
		wchar_t tmp1[32] = { 0 };
		const char *format = data->type == INTEGER ? "%lld" : "%lf";
		sprintf(tmp, format, data->value.integer);
		int len = strlen(tmp);
		for (int i = 0; i < len; ++i)
			tmp1[i] = tmp[i];
		strlist_append(head, tmp1, len);
		break;
	}
	case BOOLEAN:
	{
		int len = data->value.boolen ? 4 : 5;
		wchar_t *value = data->value.boolen ? L"true" : L"false";
		strlist_append(head, value, len);
		break;
	}
	case TEXT:
	{
		strlist_append(head, L"\"", 1);
		strlist_append(head, data->value.text, wcslen(data->value.text));
		strlist_append(head, L"\"", 1);
		break;
	}
	case TABLE:
	{
		json_object *item = data->value.item;
		int index = 0;
		while (item)
		{
			if(index) strlist_append(head, L",\"", 2);
			else strlist_append(head, L"{\"", 2);
			strlist_append(head, item->key, wcslen(item->key));
			strlist_append(head, L"\":", 2);
			to_string(item,head);

			item = item->next;
			++index;
		}
		strlist_append(head, L"}", 1);
		break;
	}
	case ARRAY:
	{
		json_object *item = data->value.item;
		int index = 0;
		while (item)
		{
			strlist_append(head, index ? L"," : L"[", 1);
			to_string(item, head);
			item = item->next;
			++index;
		}
		strlist_append(head, L"]", 1);
		break;
	}
	}
}

wchar_t *json_serialize(json_object *data)
{
	strlist head = { 0 };
	to_string(data, &head);
	wchar_t *string = strlist_to_string(&head);	
	return string;
}
