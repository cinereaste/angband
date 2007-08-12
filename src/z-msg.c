/*
 * File: z-msg.c
 * Purpose: Message handling
 *
 * Copyright (c) 2007 Elly
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "z-virt.h"
#include "z-term.h"
#include "z-msg.h"

/* Maximum number of messages to remember */
#define MESSAGE_MAX		2048

typedef struct _message_t
{
	char *str;
	struct _message_t *prev;
	struct _message_t *next;
	u16b type;
	u16b count;
} message_t;

typedef struct _msgcolor_t
{
	u16b type;
	byte color;
	struct _msgcolor_t *next;
} msgcolor_t;

typedef struct _msgqueue_t
{
	message_t *head;
	message_t *tail;
	msgcolor_t *colors;
	u32b count;
} msgqueue_t;

static msgqueue_t *messages = NULL;

/* Functions operating on the entire list */
errr messages_init(void)
{
	messages = ZNEW(msgqueue_t);
	return 0;
}

void messages_free(void)
{
	msgcolor_t *c = messages->colors;
	msgcolor_t *nextc;
	message_t *m = messages->head;
	message_t *nextm;

	while (m)
	{
		nextm = m->next;
		FREE(m->str);
		FREE(m);
		m = nextm;
	}

	while (c)
	{
		nextc = c->next;
		FREE(c);
		c = nextc;
	}

	FREE(messages);
}

u16b messages_num(void)
{
	return messages->count;
}

/* Functions for individual messages */

void message_add(const char *str, u16b type)
{
	message_t *m;

	if (messages->head &&
	    messages->head->type == type &&
	    !strcmp(messages->head->str, str))
	{
	    	messages->head->count++;
		return;
	}

	m = ZNEW(message_t);
	m->str = string_make(str);
	m->type = type;
	m->count = 1;
	m->next = messages->head;

	messages->count++;
	messages->head = m;

	if (messages->count > MESSAGE_MAX)
	{
		message_t *m = messages->tail;
		FREE(m->str);
		messages->tail = m->prev;
		FREE(m);
		messages->count--;
	}
}

static message_t *message_get(u16b age)
{
	message_t *m = messages->head;

	while (m && age--)
		m = m->next;

	return m;
}


const char *message_str(u16b age)
{
	message_t *m = message_get(age);
	return (m ? m->str : "");
}

u16b message_count(u16b age)
{
	message_t *m = message_get(age);
	return (m ? m->count : 0);
}

u16b message_type(u16b age)
{
	message_t *m = message_get(age);
	return (m ? m->type : 0);
}

byte message_color(u16b age)
{
	message_t *m = message_get(age);
	return (m ? message_type_color(m->type) : TERM_WHITE);
}


/* Message-color functions */

errr message_color_define(u16b type, byte color)
{
	msgcolor_t *mc = messages->colors;

	if (!mc)
	{
		messages->colors = ZNEW(msgcolor_t);
		messages->colors->type = type;
		messages->colors->color = color;
		return 0;
	}

	while (mc->next)
	{
		if (mc->type == type)
		{
			mc->color = color;
			return 0;
		}
		mc = mc->next;
	}

	mc->next = ZNEW(msgcolor_t);
	mc->next->type = type;
	mc->next->color = color;

	return 0;
}

byte message_type_color(u16b type)
{
	msgcolor_t *mc = messages->colors;
	byte color = TERM_WHITE;

	while (mc && mc->type != type)
		mc = mc->next;

	if (mc && (mc->color != TERM_DARK))
		color = mc->color;

	return color;
}