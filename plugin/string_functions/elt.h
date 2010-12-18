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

#ifndef PLUGIN_STRING_FUNCTIONS_ELT_H
#define PLUGIN_STRING_FUNCTIONS_ELT_H

#include <drizzled/function/str/strfunc.h>

namespace drizzled
{

class Item_func_elt :public Item_str_func
{
public:
  Item_func_elt() :Item_str_func() {}
  double val_real();
  int64_t val_int();
  String *val_str(String *str);
  void fix_length_and_dec();
  const char *func_name() const { return "elt"; }
  bool check_argument_count(int n) { return n > 1; }
};

} /* namespace drizzled */

#endif /* PLUGIN_STRING_FUNCTIONS_ELT_H */
