/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <drizzled/server_includes.h>
#include CSTDINT_H
#include <drizzled/show.h>
#include <drizzled/table.h>
#include <drizzled/item/ident.h>

const uint32_t NO_CACHED_FIELD_INDEX= UINT32_MAX;

Item_ident::Item_ident(Name_resolution_context *context_arg,
                       const char *db_name_arg,const char *table_name_arg,
                       const char *field_name_arg)
  :orig_db_name(db_name_arg), orig_table_name(table_name_arg),
   orig_field_name(field_name_arg), context(context_arg),
   db_name(db_name_arg), table_name(table_name_arg),
   field_name(field_name_arg),
   alias_name_used(false), cached_field_index(NO_CACHED_FIELD_INDEX),
   cached_table(0), depended_from(0)
{
  name = (char*) field_name_arg;
}

/**
  Constructor used by Item_field & Item_*_ref (see Item comment)
*/

Item_ident::Item_ident(Session *session, Item_ident *item)
  :Item(session, item),
   orig_db_name(item->orig_db_name),
   orig_table_name(item->orig_table_name),
   orig_field_name(item->orig_field_name),
   context(item->context),
   db_name(item->db_name),
   table_name(item->table_name),
   field_name(item->field_name),
   alias_name_used(item->alias_name_used),
   cached_field_index(item->cached_field_index),
   cached_table(item->cached_table),
   depended_from(item->depended_from)
{}

void Item_ident::cleanup()
{
#ifdef CANT_BE_USED_AS_MEMORY_IS_FREED
                       db_name ? db_name : "(null)",
                       orig_db_name ? orig_db_name : "(null)",
                       table_name ? table_name : "(null)",
                       orig_table_name ? orig_table_name : "(null)",
                       field_name ? field_name : "(null)",
                       orig_field_name ? orig_field_name : "(null)"));
#endif
  Item::cleanup();
  db_name= orig_db_name;
  table_name= orig_table_name;
  field_name= orig_field_name;
  depended_from= 0;
  return;
}

bool Item_ident::remove_dependence_processor(unsigned char * arg)
{
  if (depended_from == (st_select_lex *) arg)
    depended_from= 0;
  return(0);
}

const char *Item_ident::full_name() const
{
  char *tmp;
  if (!table_name || !field_name)
    return field_name ? field_name : name ? name : "tmp_field";
  if (db_name && db_name[0])
  {
    tmp=(char*) sql_alloc((uint) strlen(db_name)+(uint) strlen(table_name)+
                          (uint) strlen(field_name)+3);
    strxmov(tmp,db_name,".",table_name,".",field_name,NULL);
  }
  else
  {
    if (table_name[0])
    {
      tmp= (char*) sql_alloc((uint) strlen(table_name) +
                             (uint) strlen(field_name) + 2);
      strxmov(tmp, table_name, ".", field_name, NULL);
    }
    else
      tmp= (char*) field_name;
  }
  return tmp;
}


void Item_ident::print(String *str,
                       enum_query_type)
{
  Session *session= current_session;
  char d_name_buff[MAX_ALIAS_NAME], t_name_buff[MAX_ALIAS_NAME];
  const char *d_name= db_name, *t_name= table_name;
  if (lower_case_table_names== 1 ||
      (lower_case_table_names == 2 && !alias_name_used))
  {
    if (table_name && table_name[0])
    {
      strcpy(t_name_buff, table_name);
      my_casedn_str(files_charset_info, t_name_buff);
      t_name= t_name_buff;
    }
    if (db_name && db_name[0])
    {
      strcpy(d_name_buff, db_name);
      my_casedn_str(files_charset_info, d_name_buff);
      d_name= d_name_buff;
    }
  }

  if (!table_name || !field_name || !field_name[0])
  {
    const char *nm= (field_name && field_name[0]) ?
                      field_name : name ? name : "tmp_field";
    append_identifier(session, str, nm, (uint) strlen(nm));
    return;
  }
  if (db_name && db_name[0] && !alias_name_used)
  {
    {
      append_identifier(session, str, d_name, (uint)strlen(d_name));
      str->append('.');
    }
    append_identifier(session, str, t_name, (uint)strlen(t_name));
    str->append('.');
    append_identifier(session, str, field_name, (uint)strlen(field_name));
  }
  else
  {
    if (table_name[0])
    {
      append_identifier(session, str, t_name, (uint) strlen(t_name));
      str->append('.');
      append_identifier(session, str, field_name, (uint) strlen(field_name));
    }
    else
      append_identifier(session, str, field_name, (uint) strlen(field_name));
  }
}

double Item_ident_for_show::val_real()
{
  return field->val_real();
}


int64_t Item_ident_for_show::val_int()
{
  return field->val_int();
}


String *Item_ident_for_show::val_str(String *str)
{
  return field->val_str(str);
}


my_decimal *Item_ident_for_show::val_decimal(my_decimal *dec)
{
  return field->val_decimal(dec);
}

void Item_ident_for_show::make_field(Send_field *tmp_field)
{
  tmp_field->table_name= tmp_field->org_table_name= table_name;
  tmp_field->db_name= db_name;
  tmp_field->col_name= tmp_field->org_col_name= field->field_name;
  tmp_field->charsetnr= field->charset()->number;
  tmp_field->length=field->field_length;
  tmp_field->type=field->type();
  tmp_field->flags= field->table->maybe_null ?
    (field->flags & ~NOT_NULL_FLAG) : field->flags;
  tmp_field->decimals= field->decimals();
}

