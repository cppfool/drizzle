/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Monty Taylor
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#ifndef PLUGIN_ERROR_DICTIONARY_ERRORS_H
#define PLUGIN_ERROR_DICTIONARY_ERRORS_H

#include <drizzled/plugin/table_function.h>
#include <drizzled/error.h>

namespace drizzle_plugin
{
namespace error_dictionary
{

class Errors :
  public drizzled::plugin::TableFunction
{
public:
  Errors();

  class Generator :
    public drizzled::plugin::TableFunction::Generator 
  {
    const drizzled::ErrorMap::ErrorMessageMap& _error_map;
    drizzled::ErrorMap::ErrorMessageMap::const_iterator _iter;
  public:
    Generator(drizzled::Field **arg);

    bool populate();
  };

  Generator *generator(drizzled::Field **arg)
  {
    return new Generator(arg);
  }
};

} /* namespace error_dictionary */
} /* namespace drizzle_plugin */

#endif /* PLUGIN_ERROR_DICTIONARY_ERRORS_H */