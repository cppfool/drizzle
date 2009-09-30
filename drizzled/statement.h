/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#ifndef DRIZZLED_STATEMENT_H
#define DRIZZLED_STATEMENT_H

#include <drizzled/server_includes.h>
#include <drizzled/definitions.h>
#include <drizzled/error.h>
#include <drizzled/sql_parse.h>
#include <drizzled/sql_base.h>
#include <drizzled/show.h>

class Session;
class TableList;
class Item;

namespace drizzled
{
namespace statement
{

/**
 * @class Statement
 * @brief Represents a statement to be executed
 */
class Statement
{
public:
  Statement(Session *in_session)
    : 
      session(in_session)
  {}

  virtual ~Statement() {}

  /**
   * Execute the statement.
   *
   * @return true on failure; false on success
   */
  virtual bool execute()= 0;

protected:

  /**
   * A session handler.
   */
  Session *session;
};

} /* end namespace statement */

} /* end namespace drizzled */

#endif /* DRIZZLED_STATEMENT_H */