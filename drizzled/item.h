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



#ifndef DRIZZLED_ITEM_H
#define DRIZZLED_ITEM_H

#include <drizzled/global_charset_info.h>
#include <drizzled/dtcollation.h>
#include <drizzled/type/time.h>
#include <drizzled/type/decimal.h>
#include <drizzled/sql_list.h>
#include <drizzled/memory/sql_alloc.h>
#include <drizzled/table.h>
#include <drizzled/item_result.h>

#include "drizzled/visibility.h"

namespace drizzled
{

class TableList;
class Item_field;
class Name_resolution_context;
class Select_Lex;
class Item_equal;
class user_var_entry;
class Item_sum;
class Item_in_subselect;
class SendField;
class Field;

namespace plugin
{
class Client;
}

/**
  Dummy error processor used by default by Name_resolution_context.

  @note
    do nothing
*/
void dummy_error_processor(Session *session, void *data);

/*
  Analyzer function
    SYNOPSIS
      argp   in/out IN:  Analysis parameter
                    OUT: Parameter to be passed to the transformer

     RETURN
      true   Invoke the transformer
      false  Don't do it

*/
typedef bool (Item::*Item_analyzer) (unsigned char **argp);
typedef Item* (Item::*Item_transformer) (unsigned char *arg);
typedef void (*Cond_traverser) (const Item *item, void *arg);
typedef bool (Item::*Item_processor) (unsigned char *arg);

/**
 * The Item class is the base class for all items in the parsed
 * statement "tree" or Lex.  Each item represents something in the
 * execution plan.
 */
class DRIZZLED_API Item : public memory::SqlAlloc
{
  /* Prevent use of these */
  Item(const Item &);
  void operator=(Item &);

  /* Cache of the result of is_expensive(). */
  int8_t is_expensive_cache;
  virtual bool is_expensive_processor(unsigned char *arg);

public:

  enum Type
  {
    FIELD_ITEM= 0,
    FUNC_ITEM,
    SUM_FUNC_ITEM,
    STRING_ITEM,
    INT_ITEM,
    REAL_ITEM,
    NULL_ITEM,
    VARBIN_ITEM,
    COPY_STR_ITEM,
    FIELD_AVG_ITEM,
    DEFAULT_VALUE_ITEM,
    PROC_ITEM,
    COND_ITEM,
    REF_ITEM,
    FIELD_STD_ITEM,
    FIELD_VARIANCE_ITEM,
    INSERT_VALUE_ITEM,
    SUBSELECT_ITEM,
    ROW_ITEM, CACHE_ITEM,
    TYPE_HOLDER,
    PARAM_ITEM,
    BOOLEAN_ITEM,
    DECIMAL_ITEM
  };
  enum traverse_order
  {
    T_POSTFIX,
    T_PREFIX
  };
  enum cond_result
  {
    COND_UNDEF,
    COND_OK,
    COND_TRUE,
    COND_FALSE
  };

  /**
   *  str_values's main purpose is to be used to cache the value in
   *  save_in_field
   */
  String str_value;

  /** Name from select */
  char *name;

  /** Length of name */
  uint32_t name_length;

  /** Original item name (if it was renamed) */
  char *orig_name;
  Item *next;
  uint32_t max_length;

  int8_t marker;
  uint8_t decimals;
  bool fixed; /**< If item fixed with fix_fields */
  bool maybe_null; /**< True if item may be null */
  bool null_value; /**< True if item is null */
  bool unsigned_flag;

  bool is_unsigned() const
  {
    return unsigned_flag;
  }

  virtual bool negative() const
  {
    return false;
  }

  bool with_sum_func;
  bool is_autogenerated_name; /**< indicates whether name of this Item was autogenerated or set by user */

  /**
   * If this item is a subselect or some of its arguments is or contains a
   * subselect. Computed by fix_fields.
   */
  bool with_subselect;
  DTCollation collation;
  Item_result cmp_context; /**< Comparison context */

  /**
   * Constructor
   *
   * @note
   *
   * Alloc & destruct is done as start of select using memory::sql_alloc
   */
  Item();

  /**
   * Constructor used by Item_field, Item_ref & aggregate (sum) functions.
   *
   * Used for duplicating lists in processing queries with temporary
   * tables.
   *
   * Also it used for Item_cond_and/Item_cond_or for creating
   * top AND/OR structure of WHERE clause to protect it of
   * optimisation changes in prepared statements
   */
  Item(Session *session, Item *item);

  virtual ~Item()
  {
#ifdef EXTRA_DEBUG
    name= NULL;
#endif
  }

  void set_name(const std::string &arg)
  {
    set_name(arg.c_str(), arg.length(), system_charset_info);
  }

  void set_name(const char *str, uint32_t length, const CHARSET_INFO * const cs= system_charset_info);
  /**
   * Renames item (used for views, cleanup() return original name).
   *
   * @param new_name	new name of item;
   */
  void rename(char *new_name);
  void init_make_field(SendField *tmp_field,enum enum_field_types type);
  virtual void cleanup();
  virtual void make_field(SendField *field);
  /**
    Create a field to hold a string value from an item.

    If max_length > CONVERT_IF_BIGGER_TO_BLOB create a blob @n
    If max_length > 0 create a varchar @n
    If max_length == 0 create a CHAR(0)

    @param table		Table for which the field is created
  */
  Field *make_string_field(Table *table);
  virtual bool fix_fields(Session *, Item **);

  /**
   * Fix after some tables has been pulled out. Basically re-calculate all
   * attributes that are dependent on the tables.
   */
  virtual void fix_after_pullout(Select_Lex *new_parent, Item **ref);

  /**
   * Should be used in case where we are sure that we do not need
   * complete fix_fields() procedure.
   */
  inline void quick_fix_field()
  {
    fixed= true;
  }

  virtual int save_in_field(Field *field, bool no_conversions);
  virtual void save_org_in_field(Field *field)
  {
    (void) save_in_field(field, true);
  }
  virtual int save_safe_in_field(Field *field)
  {
    return save_in_field(field, true);
  }
  /**
   * This is only called from items that is not of type item_field.
   */
  virtual bool send(plugin::Client *client, String *str);
  /**
    Compares this Item to another Item, returning true if Item's
    are functionally equal.

    @details

    This function is called when:
    - Comparing items in the WHERE clause (when doing where optimization)
    - When trying to find an order_st BY/GROUP BY item in the SELECT part
  */
  virtual bool eq(const Item *, bool binary_cmp) const;
  virtual Item_result result_type() const
  {
    return REAL_RESULT;
  }
  virtual Item_result cast_to_int_type() const
  {
    return result_type();
  }
  virtual enum_field_types string_field_type() const;
  virtual enum_field_types field_type() const;
  virtual enum Type type() const =0;

  /**
   * Converts
   *  "func_arg $CMP$ const" half-interval
   * into
   *  "FUNC(func_arg) $CMP2$ const2"
   *
   * @details
   *
   * left_endp  false  <=> The interval is "x < const" or "x <= const"
   *            true   <=> The interval is "x > const" or "x >= const"
   *
   * incl_endp  IN   true <=> the comparison is '<' or '>'
   *                false <=> the comparison is '<=' or '>='
   *            OUT  The same but for the "F(x) $CMP$ F(const)" comparison
   *
   * This function is defined only for unary monotonic functions. The caller
   * supplies the source half-interval
   *
   * x $CMP$ const
   *
   * The value of const is supplied implicitly as the value this item's
   * argument, the form of $CMP$ comparison is specified through the
   * function's arguments. The calle returns the result interval
   *
   * F(x) $CMP2$ F(const)
   *
   * passing back F(const) as the return value, and the form of $CMP2$
   * through the out parameter. NULL values are assumed to be comparable and
   * be less than any non-NULL values.
   *
   * @retval
   *
   * The output range bound, which equal to the value of val_int()
   * - If the value of the function is NULL then the bound is the
   * smallest possible value of INT64_MIN
   */
  virtual int64_t val_int_endpoint(bool left_endp, bool *incl_endp);

  /* valXXX methods must return NULL or 0 or 0.0 if null_value is set. */
  /**
   * Returns double precision floating point representation of item.
   *
   * @retval
   *
   * In case of NULL value return 0.0 and set null_value flag to true.
   * If value is not null null_value flag will be reset to false.
   */
  virtual double val_real()=0;
  /**
   * Returns integer representation of item.
   *
   * @retval
   *
   * In case of NULL value return 0 and set null_value flag to true.
   * If value is not null null_value flag will be reset to false.
   */
  virtual int64_t val_int()=0;
  /**
   * This is just a shortcut to avoid the cast. You should still use
   * unsigned_flag to check the sign of the item.
   */
  inline uint64_t val_uint()
  {
    return (uint64_t) val_int();
  }
  /**
   * Return string representation of this item object.
   *
   * @param an allocated buffer this or any nested Item object can use to
   *        store return value of this method.
   *
   * @note
   *
   * Buffer passed via argument  should only be used if the item itself
   * doesn't have an own String buffer. In case when the item maintains
   * it's own string buffer, it's preferable to return it instead to
   * minimize number of mallocs/memcpys.
   *
   * The caller of this method can modify returned string, but only in case
   * when it was allocated on heap, (is_alloced() is true).  This allows
   * the caller to efficiently use a buffer allocated by a child without
   * having to allocate a buffer of it's own. The buffer, given to
   * val_str() as argument, belongs to the caller and is later used by the
   * caller at it's own choosing.
   *
   * A few implications from the above:
   *  - unless you return a string object which only points to your buffer
   *    but doesn't manages it you should be ready that it will be
   *    modified.
   *  - even for not allocated strings (is_alloced() == false) the caller
   *    can change charset (see Item_func_{typecast/binary}. XXX: is this
   *    a bug?
   *  - still you should try to minimize data copying and return internal
   *    object whenever possible.
   *
   * @retval
   *   In case of NULL value return 0 (NULL pointer) and set null_value flag
   *   to true.
   *   If value is not null null_value flag will be reset to false.
   */
  virtual String *val_str(String *str)=0;

  /**
   * Return decimal representation of item with fixed point.
   *
   * @param buffer which can be used by Item for returning value
   *        (but can be not)
   *
   * @note
   *
   * Returned value should not be changed if it is not the same which was
   * passed via argument.
   *
   * @retval
   *
   * Return pointer on type::Decimal (it can be other then passed via argument)
   * if value is not NULL (null_value flag will be reset to false).
   * In case of NULL value it return 0 pointer and set null_value flag
   * to true.
   */
  virtual type::Decimal *val_decimal(type::Decimal *decimal_buffer)= 0;

  /**
   * Return boolean value of item.
   *
   * @retval
   *
   * false value is false or NULL
   * true value is true (not equal to 0)
   */
  virtual bool val_bool();

  /* Helper functions, see item_sum.cc */
  String *val_string_from_real(String *str);
  String *val_string_from_int(String *str);
  String *val_string_from_decimal(String *str);
  type::Decimal *val_decimal_from_real(type::Decimal *decimal_value);
  type::Decimal *val_decimal_from_int(type::Decimal *decimal_value);
  type::Decimal *val_decimal_from_string(type::Decimal *decimal_value);
  type::Decimal *val_decimal_from_date(type::Decimal *decimal_value);
  type::Decimal *val_decimal_from_time(type::Decimal *decimal_value);
  int64_t val_int_from_decimal();
  double val_real_from_decimal();

  bool save_time_in_field(Field *field);
  bool save_date_in_field(Field *field);

  /**
   * Stores a string value in field directly
   *
   * @details
   *
   * The method is used by Item_*::save_in_field implementations
   * when we don't need to calculate the value to store
   *
   * @see Item_string::save_in_field() implementation for example
   *
   * @param Pointer to field where to store
   * @param Pointer to the string value to be stored
   *
   * @retval
   *  Nonzero value if error
   *
   */
  int save_str_value_in_field(Field *field, String *result);

  virtual Field *get_tmp_table_field(void)
  {
    return NULL;
  }
  /* This is also used to create fields in CREATE ... SELECT: */
  virtual Field *tmp_table_field(Table *t_arg);
  virtual const char *full_name(void) const;

  /*
    *result* family of methods is analog of *val* family (see above) but
    return value of result_field of item if it is present. If Item have not
    result field, it return val(). This methods set null_value flag in same
    way as *val* methods do it.
  */
  virtual double  val_result() 
  {
    return val_real();
  }
  virtual int64_t val_int_result()
  {
    return val_int();
  }
  virtual String *str_result(String* tmp)
  {
    return val_str(tmp);
  }
  virtual type::Decimal *val_decimal_result(type::Decimal *val)
  {
    return val_decimal(val);
  }
  virtual bool val_bool_result()
  {
    return val_bool();
  }
  /**
   * Returns bit map of tables used by item
   */
  virtual table_map used_tables() const
  {
    return (table_map) 0L;
  }
  /**
    Return table map of tables that can't be NULL tables (tables that are
    used in a context where if they would contain a NULL row generated
    by a LEFT or RIGHT join, the item would not be true).
    This expression is used on WHERE item to determinate if a LEFT JOIN can be
    converted to a normal join.
    Generally this function should return used_tables() if the function
    would return null if any of the arguments are null
    As this is only used in the beginning of optimization, the value don't
    have to be updated in update_used_tables()
  */
  virtual table_map not_null_tables() const
  {
    return used_tables();
  }
  /**
    Returns true if this is a simple constant item like an integer, not
    a constant expression. Used in the optimizer to propagate basic constants.
  */
  virtual bool basic_const_item() const
  {
    return false;
  }
  /* cloning of constant items (NULL if it is not const) */
  virtual Item *clone_item()
  {
    return NULL;
  }
  virtual cond_result eq_cmp_result() const
  {
    return COND_OK;
  }
  uint32_t float_length(uint32_t decimals_par) const;
  virtual uint32_t decimal_precision() const;
  int decimal_int_part() const;

  /**
    Returns true if this is constant (during query execution, i.e. its value
    will not change until next fix_fields) and its value is known.
  */
  virtual bool const_item() const
  {
    return used_tables() == 0;
  }
  /**
    Returns true if this is constant but its value may be not known yet.
    (Can be used for parameters of prep. stmts or of stored procedures.)
  */
  virtual bool const_during_execution() const
  {
    return (used_tables() & ~PARAM_TABLE_BIT) == 0;
  }

  /**
    This method is used for to:
      - to generate a view definition query (SELECT-statement);
      - to generate a SQL-query for EXPLAIN EXTENDED;
      - to generate a SQL-query to be shown in INFORMATION_SCHEMA;
      - debug.

    For more information about view definition query, INFORMATION_SCHEMA
    query and why they should be generated from the Item-tree, @see
    register_view().
  */
  virtual void print(String *str, enum_query_type query_type);

  void print_item_w_name(String *, enum_query_type query_type);
  virtual void update_used_tables() {}
  virtual void split_sum_func(Session *session, 
                              Item **ref_pointer_array,
                              List<Item> &fields);
  /**
    Move SUM items out from item tree and replace with reference.

    @param session			Thread handler
    @param ref_pointer_array	Pointer to array of reference fields
    @param fields		All fields in select
    @param ref			Pointer to item
    @param skip_registered       <=> function be must skipped for registered
                                SUM items

    @note
      This is from split_sum_func() for items that should be split

      All found SUM items are added FIRST in the fields list and
      we replace the item with a reference.

      session->fatal_error() may be called if we are out of memory
  */
  void split_sum_func(Session *session, 
                      Item **ref_pointer_array,
                      List<Item> &fields,
                      Item **ref, 
                      bool skip_registered);

  /**
    Get the value of the function as a type::Time structure.
    As a extra convenience the time structure is reset on error!
  */
  virtual bool get_date(type::Time &ltime, uint32_t fuzzydate);
  /**
    Get time of first argument.

    As a extra convenience the time structure is reset on error!
  */
  virtual bool get_time(type::Time &ltime);
  virtual bool get_date_result(type::Time &ltime,uint32_t fuzzydate);

  /**
    The method allows to determine nullness of a complex expression
    without fully evaluating it, instead of calling val/result*() then
    checking null_value. Used in Item_func_isnull/Item_func_isnotnull
    and Item_sum_count/Item_sum_count_distinct.
    Any new item which can be NULL must implement this method.
  */
  virtual bool is_null();

  /** Make sure the null_value member has a correct value. */
  virtual void update_null_value ();

  /**
    Inform the item that there will be no distinction between its result
    being false or NULL.

    @note

      This function will be called for eg. Items that are top-level AND-parts
      of the WHERE clause. Items implementing this function (currently
      Item_cond_and and subquery-related item) enable special optimizations
      when they are "top level".
  */
  virtual void top_level_item(void);
  /**
   * Sets field of temporary table for Item which can be switched on temporary
   * table during query processing (grouping and so on)
   */
  virtual void set_result_field(Field *field);
  virtual bool is_result_field(void);
  virtual bool is_bool_func(void);
  virtual void save_in_result_field(bool no_conversions);

  /**
   * Sets value of aggregate function in case of no rows for grouping were found
   */
  virtual void no_rows_in_result(void);
  virtual Item *copy_or_same(Session *session);

  virtual Item *copy_andor_structure(Session *session);

  virtual Item *real_item(void);
  virtual const Item *real_item(void) const;
  virtual Item *get_tmp_table_item(Session *session);

  static const CHARSET_INFO *default_charset();
  virtual const CHARSET_INFO *compare_collation();

  virtual bool walk(Item_processor processor,
                    bool walk_subquery,
                    unsigned char *arg);

  /**
    Traverse item tree possibly transforming it (replacing items).

    If you don't need to transform an item tree, but only traverse
    it, please use Item::walk() instead.

    @param transformer    functor that performs transformation of a subtree
    @param arg            opaque argument passed to the functor

    @return
      Returns pointer to the new subtree root.  Session::change_item_tree()
      should be called for it if transformation took place, i.e. if a
      pointer to newly allocated item is returned.
  */
  virtual Item* transform(Item_transformer transformer, unsigned char *arg);

  /**
    This function performs a generic "compilation" of the Item tree.
    The process of compilation is assumed to go as follows:

    @code
    compile()
    {
      if (this->*some_analyzer(...))
      {
        compile children if any;
        this->*some_transformer(...);
      }
    }
    @endcode

    i.e. analysis is performed top-down while transformation is done
    bottom-up.
  */
  virtual Item* compile(Item_analyzer analyzer, 
                        unsigned char **arg_p,
                        Item_transformer transformer, 
                        unsigned char *arg_t);

  virtual void traverse_cond(Cond_traverser traverser,
                             void *arg,
                             traverse_order order);

  virtual bool remove_dependence_processor(unsigned char * arg);
  virtual bool remove_fixed(unsigned char * arg);
  virtual bool collect_item_field_processor(unsigned char * arg);
  virtual bool find_item_in_field_list_processor(unsigned char *arg);
  virtual bool change_context_processor(unsigned char *context);
  virtual bool register_field_in_read_map(unsigned char *arg);
  virtual bool subst_argument_checker(unsigned char **arg);

  virtual bool cache_const_expr_analyzer(unsigned char **arg);
  virtual Item* cache_const_expr_transformer(unsigned char *arg);

  virtual Item *equal_fields_propagator(unsigned char * arg);
  virtual bool set_no_const_sub(unsigned char *arg);
  virtual Item *replace_equal_field(unsigned char * arg);

  // Row emulation
  virtual uint32_t cols();
  virtual Item* element_index(uint32_t i);
  virtual Item** addr(uint32_t i);
  virtual bool check_cols(uint32_t c);
  // It is not row => null inside is impossible
  virtual bool null_inside();
  // used in row subselects to get value of elements
  virtual void bring_value();

  /**
    Create a field based on field_type of argument.

    For now, this is only used to create a field for
    IFNULL(x,something) and time functions

    @retval
      NULL  error
    @retval
      \#    Created field
  */
  Field *tmp_table_field_from_field_type(Table *table, bool fixed_length);

  virtual Item *neg_transformer(Session *session);
  virtual Item *update_value_transformer(unsigned char *select_arg);
  virtual Item *safe_charset_converter(const CHARSET_INFO * const tocs);
  void delete_self();

  /**
   * Returns true for Items representing DATE/TIME functions and DATE/TIME table fields.
   * Those Items have result_type()==STRING_RESULT (and not INT_RESULT), but
   * their values should be compared as integers (because the integer
   * representation is more precise than the string one).
   */
  virtual bool result_as_int64_t();
  bool is_datetime();

  /**
   * Tests whether an expression is expensive to compute. Used during
   * optimization to avoid computing expensive expressions during this
   * phase. Also used to force temp tables when sorting on expensive
   * functions.
   *
   * @todo
   * 
   * Normally we should have a method:
   * cost Item::execution_cost(),
   * where 'cost' is either 'double' or some structure of various cost
   * parameters.
   *
   *NOTE
   *   This function is now used to prevent evaluation of materialized IN
   *   subquery predicates before it is allowed. grep for
   *   DontEvaluateMaterializedSubqueryTooEarly to see the uses.
   */
  virtual bool is_expensive();

  String *check_well_formed_result(String *str, bool send_error= 0);
  /**
   * Compares two items using a given collation
   *
   * @details
   *
   *  This method works exactly as Item::eq if the collation cs coincides with
   *  the collation of the compared objects. Otherwise, first the collations that
   *  differ from cs are replaced for cs and then the items are compared by
   *  Item::eq. After the comparison the original collations of items are
   *  restored.
   *
   * @param Pointer to the item to compare with
   * @param Compare as binary?
   * @param Pointer to the collation to use when comparing strings
   *
   * @retval
   *  true if compared items has been detected as equal
   * @retval
   *  false otherwise
   */
  bool eq_by_collation(Item *item, bool binary_cmp, const CHARSET_INFO * const cs);

  inline uint32_t char_to_byte_length_safe(uint32_t char_length_arg, uint32_t mbmaxlen_arg)
  { 
    uint64_t tmp= ((uint64_t) char_length_arg) * mbmaxlen_arg;
    return (tmp > UINT32_MAX) ? (uint32_t) UINT32_MAX : (uint32_t) tmp;
  } 

  uint32_t max_char_length() const
  {
    return max_length / collation.collation->mbmaxlen;
  }

  void fix_length_and_charset(uint32_t max_char_length_arg, CHARSET_INFO *cs)
  { 
    max_length= char_to_byte_length_safe(max_char_length_arg, cs->mbmaxlen);
    collation.collation= cs;
  }

  void fix_char_length(uint32_t max_char_length_arg)
  { 
    max_length= char_to_byte_length_safe(max_char_length_arg,
                                         collation.collation->mbmaxlen);
  }

  void fix_char_length_uint64_t(uint64_t max_char_length_arg)
  { 
    uint64_t max_result_length= max_char_length_arg *
      collation.collation->mbmaxlen;

    if (max_result_length >= MAX_BLOB_WIDTH)
    { 
      max_length= MAX_BLOB_WIDTH;
      maybe_null= false;
    }
    else
    {
      max_length= max_result_length;
    }
  }

  void fix_length_and_charset_datetime(uint32_t max_char_length_arg)
  { 
    collation.set(&my_charset_bin);
    fix_char_length(max_char_length_arg);
  }
protected:
  Session &getSession()
  {
    return _session;
  }

private:
  Session &_session;
};

namespace display {
const std::string &type(Item::Type type);
} /* namespace display */

std::ostream& operator<<(std::ostream& output, const Item &item);

} /* namespace drizzled */

/** @TODO Why is this in the middle? */
#include <drizzled/item/ident.h>

namespace drizzled
{

/**
  Mark item and Select_Lexs as dependent if item was resolved in
  outer SELECT.

  @param session             thread handler
  @param last            select from which current item depend
  @param current         current select
  @param resolved_item   item which was resolved in outer SELECT(for warning)
  @param mark_item       item which should be marked (can be differ in case of
                         substitution)
*/
void mark_as_dependent(Session *session,
		       Select_Lex *last,
                       Select_Lex *current,
                       Item_ident *resolved_item,
                       Item_ident *mark_item);

/**
  Resolve a column reference in a sub-select.

  Resolve a column reference (usually inside a HAVING clause) against the
  SELECT and GROUP BY clauses of the query described by 'select'. The name
  resolution algorithm searches both the SELECT and GROUP BY clauses, and in
  case of a name conflict prefers GROUP BY column names over SELECT names. If
  both clauses contain different fields with the same names, a warning is
  issued that name of 'ref' is ambiguous. We extend ANSI SQL in that when no
  GROUP BY column is found, then a HAVING name is resolved as a possibly
  derived SELECT column. This extension is allowed only if the
  MODE_ONLY_FULL_GROUP_BY sql mode isn't enabled.

  @param session     current thread
  @param ref     column reference being resolved
  @param select  the select that ref is resolved against

  @note
    The resolution procedure is:
    - Search for a column or derived column named col_ref_i [in table T_j]
    in the SELECT clause of Q.
    - Search for a column named col_ref_i [in table T_j]
    in the GROUP BY clause of Q.
    - If found different columns with the same name in GROUP BY and SELECT
    - issue a warning and return the GROUP BY column,
    - otherwise
    - if the MODE_ONLY_FULL_GROUP_BY mode is enabled return error
    - else return the found SELECT column.


  @return
    - NULL - there was an error, and the error was already reported
    - not_found_item - the item was not resolved, no error was reported
    - resolved item - if the item was resolved
*/
Item** resolve_ref_in_select_and_group(Session *session, Item_ident *ref, Select_Lex *select);

/**
  Mark range of selects and resolved identifier (field/reference)
  item as dependent.

  @param session             thread handler
  @param last_select     select where resolved_item was resolved
  @param current_sel     current select (select where resolved_item was placed)
  @param found_field     field which was found during resolving
  @param found_item      Item which was found during resolving (if resolved
                         identifier belongs to VIEW)
  @param resolved_item   Identifier which was resolved

  @note
    We have to mark all items between current_sel (including) and
    last_select (excluding) as dependend (select before last_select should
    be marked with actual table mask used by resolved item, all other with
    OUTER_REF_TABLE_BIT) and also write dependence information to Item of
    resolved identifier.
*/
void mark_select_range_as_dependent(Session *session,
                                    Select_Lex *last_select,
                                    Select_Lex *current_sel,
                                    Field *found_field, Item *found_item,
                                    Item_ident *resolved_item);

extern void resolve_const_item(Session *session, Item **ref, Item *cmp_item);
/**
  Return true if the value stored in the field is equal to the const
  item.

  We need to use this on the range optimizer because in some cases
  we can't store the value in the field without some precision/character loss.
*/
extern bool field_is_equal_to_item(Field *field,Item *item);

/**
  Create field for temporary table.

  @todo:

   This is here for now because it needs the Item::Type. It should live
   in Field or Table once item.h is clean enough to actually include

  @param session		Thread handler
  @param table		Temporary table
  @param item		Item to create a field for
  @param type		Type of item (normally item->type)
  @param copy_func	If set and item is a function, store copy of item
                       in this array
  @param from_field    if field will be created using other field as example,
                       pointer example field will be written here
  @param default_field	If field has a default value field, store it here
  @param group		1 if we are going to do a relative group by on result
  @param modify_item	1 if item->result_field should point to new item.
                       This is relevent for how fill_record() is going to
                       work:
                       If modify_item is 1 then fill_record() will update
                       the record in the original table.
                       If modify_item is 0 then fill_record() will update
                       the temporary table
  @param convert_blob_length If >0 create a varstring(convert_blob_length)
                             field instead of blob.

  @retval
    0			on error
  @retval
    new_created field
*/
Field *create_tmp_field(Session *session,
                        Table *table,
                        Item *item,
                        Item::Type type,
                        Item ***copy_func,
                        Field **from_field,
                        Field **def_field,
                        bool group,
                        bool modify_item,
                        bool make_copy_field,
                        uint32_t convert_blob_length);

} /* namespace drizzled */

#endif /* DRIZZLED_ITEM_H */
