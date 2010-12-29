/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems, Inc.
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

#ifndef DRIZZLED_FUNCTION_MATH_INT_H
#define DRIZZLED_FUNCTION_MATH_INT_H

#include <drizzled/function/func.h>
#include <drizzled/sql_list.h>

namespace drizzled
{

class Item_int_func :public Item_func
{
public:
  Item_int_func() :Item_func() { max_length= 21; }
  Item_int_func(Item *a) :Item_func(a) { max_length= 21; }
  Item_int_func(Item *a,Item *b) :Item_func(a,b) { max_length= 21; }
  Item_int_func(Item *a,Item *b,Item *c) :Item_func(a,b,c)
  { max_length= 21; }
  Item_int_func(List<Item> &list) :Item_func(list) { max_length= 21; }
  Item_int_func(Session *session, Item_int_func *item) :Item_func(session, item) {}
  virtual ~Item_int_func();
  double val_real();
  String *val_str(String*str);
  enum Item_result result_type () const { return INT_RESULT; }
  void fix_length_and_dec() {}
};

} /* namespace drizzled */

#endif /* DRIZZLED_FUNCTION_MATH_INT_H */
