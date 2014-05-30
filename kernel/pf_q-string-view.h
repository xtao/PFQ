/***************************************************************
 *
 * (C) 2011-14 Nicola Bonelli <nicola.bonelli@cnit.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 ****************************************************************/

#ifndef __PF_Q_STRING_VIEW__
#define __PF_Q_STRING_VIEW__

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ctype.h>

typedef struct
{
 	const char *begin;
 	const char *end;

} string_view_t;



static inline string_view_t
null_view(void)
{
	string_view_t s = { NULL, NULL };
	return s;
}

static inline size_t
string_view_length(string_view_t str)
{
	return str.end - str.begin;
}


static inline string_view_t
make_string_view(const char *str)
{
	string_view_t s = { str, str + strlen(str) };
	return s;
}


static inline string_view_t
string_view_trim(string_view_t str)
{
	while ( isspace(*str.begin) &&
                str.begin != str.end )
                str.begin++;

	while ( str.end != str.begin &&
		isspace(*(str.end-1)) )
                str.end--;

        return str;
}


static inline char *
view_to_string(string_view_t str)
{
	size_t len = str.end - str.begin;

	char * ret = kmalloc(GFP_KERNEL, len + 1);
	if (ret) {
		strncpy(ret, str.begin, len);
		ret [len] = '\0';
	}
	return ret;
}


static inline int
string_view_sprintf(char *buffer, string_view_t str)
{
	char * p = view_to_string(str);
	int n = sprintf(buffer, "%s", p);
	kfree(p);
	return n;
}

static inline void
string_view_printk(string_view_t str)
{
	char * p = view_to_string(str);
	printk(KERN_INFO "%s\n", p);
	kfree(p);
}

#endif /* __PF_Q_STRING_VIEW__ */