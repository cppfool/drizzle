/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Definitions required for Query Logging plugin
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

#ifndef DRIZZLED_PLUGIN_LOGGING_H
#define DRIZZLED_PLUGIN_LOGGING_H

#include <string>

namespace drizzled
{
namespace plugin
{

class Logging : public Plugin
{
  Logging();
  Logging(const Logging &);
public:
  explicit Logging(std::string name_arg): Plugin(name_arg)  {}
  virtual ~Logging() {}

  /**
   * Make these no-op rather than pure-virtual so that it's easy for a plugin
   * to only implement one.
   */
  virtual bool pre(Session *) {return false;}
  virtual bool post(Session *) {return false;}

  static void add(Logging *handler);
  static void remove(Logging *handler);
  static bool pre_do(Session *session);
  static bool post_do(Session *session);
};

} /* namespace plugin */
} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_LOGGING_H */
