#ifndef TABLE_FUNCTION_INCLUDED
#define TABLE_FUNCTION_INCLUDED

/* Copyright (c) 2020, MariaDB Corporation. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335  USA */


#include <json_lib.h>

class Json_table_column;

/*
  The Json_table_nested_path represents the 'current nesting' level
  for a set of JSON_TABLE columns.
  Each column (Json_table_column instance) is linked with corresponding
  'nested path' object and gets its piece of JSON to parse during the computation
  phase.
  The root 'nested_path' is always present as a part of Table_function_json_table,
  then other 'nested_paths' can be created and linked into a tree structure when new
  'NESTED PATH' is met. The nested 'nested_paths' are linked with 'm_nested', the same-level
  'nested_paths' are linked with 'm_next_nested'.
  So for instance
    JSON_TABLE( '...', '$[*]'
       COLUMNS( a INT PATH '$.a' ,
          NESTED PATH '$.b[*]' COLUMNS (b INT PATH '$',
                                        NESTED PATH '$.c[*]' COLUMNS(x INT PATH '$')),
          NESTED PATH '$.n[*]' COLUMNS (z INT PAHT '$'))
  results in 4 'nested_path' created:
                 root          nested_b       nested_c     nested_n
  m_path           '$[*]'         '$.b[*]'        '$.c[*]'     '$.n[*]
  m_nested          &nested_b     &nested_c       NULL         NULL
  n_next_nested     NULL          &nested_n       NULL         NULL

and 4 columns created:
              a          b            x            z
  m_nest    &root      &nested_b    &nested_c    &nested_n
*/


class Json_table_nested_path : public Sql_alloc
{
public:
  bool m_null; // TRUE <=> produce SQL NULL.

  json_path_t m_path;
  json_engine_t m_engine;
  json_path_t m_cur_path;

  /* Counts the rows produced.  Value is set to the FOR ORDINALITY coluns */
  longlong m_ordinality_counter;

  /* the Json_table_nested_path that nests this. */
  Json_table_nested_path *m_parent;

  /* The head of the list of nested NESTED PATH statements. */
  Json_table_nested_path *m_nested;

  /* in the above list items are linked with the */
  Json_table_nested_path *m_next_nested;

  /*
    The pointer to the 'm_next_nested' member of the
    last item of the above list. So we can add new item to
    the list doing *m_next_nexted_hook= new_item_ptr
  */
  Json_table_nested_path **m_nested_hook;

  /*
    The NESTED PATH that is currently scanned in rnd_next.
  */
  Json_table_nested_path *m_cur_nested;
  /*
    The order of the above m_cur_nested in the list of the NESTED PATH.
    Used only to build the reference in position()/rnd_pos().
  */
  int m_n_cur_nested;

  Json_table_nested_path(Json_table_nested_path *parent_nest):
    m_parent(parent_nest), m_nested(0), m_next_nested(0),
    m_nested_hook(&m_nested) {}
  int set_path(THD *thd, const LEX_CSTRING &path);
  void scan_start(CHARSET_INFO *i_cs, const uchar *str, const uchar *end);
  int scan_next();
  int print(THD *thd, Field ***f, String *str,
            List_iterator_fast<Json_table_column> &it,
            Json_table_column **last_column);
  void get_current_position(const uchar *j_start, uchar *pos) const;
  void set_position(const uchar *j_start, const uchar *j_end, const uchar *pos);
};


class Json_table_column : public Sql_alloc
{
public:
  enum enum_type
  {
    FOR_ORDINALITY,
    PATH,
    EXISTS_PATH
  };

  enum enum_on_type
  {
    ON_EMPTY,
    ON_ERROR
  };

  enum enum_on_response
  {
    RESPONSE_NOT_SPECIFIED,
    RESPONSE_ERROR,
    RESPONSE_NULL,
    RESPONSE_DEFAULT
  };

  struct On_response
  {
  public:
    Json_table_column::enum_on_response m_response;
    LEX_CSTRING m_default;
    void respond(Json_table_column *jc, Field *f);
    int print(const char *name, String *str) const;
    bool specified() const { return m_response != RESPONSE_NOT_SPECIFIED; }
  };

  enum_type m_column_type;
  json_path_t m_path;
  On_response m_on_error;
  On_response m_on_empty;
  Create_field *m_field;
  Json_table_nested_path *m_nest;
  CHARSET_INFO *m_explicit_cs;
  CHARSET_INFO *m_defaults_cs;

  void set(enum_type ctype)
  {
    m_column_type= ctype;
  }
  int set(THD *thd, enum_type ctype, const LEX_CSTRING &path);
  Json_table_column(Create_field *f, Json_table_nested_path *nest) :
    m_field(f), m_nest(nest)
  {
    m_on_error.m_response= RESPONSE_NOT_SPECIFIED;
    m_on_empty.m_response= RESPONSE_NOT_SPECIFIED;
  }
  int print(THD *tnd, Field **f, String *str);
};


/*
  Class represents the table function, the function
  that returns the table as a result so supposed to appear
  in the FROM list of the SELECT statement.
  At the moment there is only one such function JSON_TABLE,
  so the class named after it, but should be refactored
  into the hierarchy root if we create more of that functions.

  As the parser finds the table function in the list it
  creates an instance of Table_function_json_table storing it
  into the TABLE_LIST::table_function.
  Then the ha_json_table instance is created based on it in
  the create_table_for_function().
*/
class Table_function_json_table : public Sql_alloc
{
protected:
  bool m_setup_done;
public:
  Item *m_json; /* The JSON value to be parsed. */

  /* The COLUMNS(...) part representation. */
  Json_table_nested_path m_nested_path;
  /* The list of table column definitions. */
  List<Json_table_column> m_columns;

  /*
    the JSON argument can be taken from other tables.
    We have to mark these tables as dependent so the
    mask of these dependent tables is calculated in ::setup().
  */
  table_map m_dep_tables;
  
  /*
    The 'depth' of NESTED PATH statements nesting.
    Needed to calculate the reference length.
    m_cur_depth is used in parser.
  */
  uint m_depth, m_cur_depth;

  /* used in parser. */
  Json_table_column *m_cur_json_table_column;
  CHARSET_INFO *m_text_literal_cs;

  Table_function_json_table(Item *json): m_setup_done(false),
    m_json(json), m_nested_path(0), m_depth(0), m_cur_depth(0) {}

  /*
    Used in sql_yacc.yy.
    Represents the current NESTED PATH level being parsed.
  */
  Json_table_nested_path *m_sql_nest;
  void add_nested(Json_table_nested_path *np);
  void leave_nested();

  int setup(THD *thd, TABLE_LIST *sql_table);
  /* if the table is ready to be used in Item_field::fix_fieds */
  bool ready_for_lookup() const { return m_setup_done; }
  bool join_cache_allowed() const { return !m_dep_tables; }
  void get_estimates(ha_rows *out_rows,
                     double *scan_time, double *startup_cost);
  int print(THD *thd, TABLE_LIST *sql_table,
            String *str, enum_query_type query_type);

  void fix_after_pullout(TABLE_LIST *sql_table,
                         st_select_lex *new_parent, bool merge);
};


TABLE *create_table_for_function(THD *thd, TABLE_LIST *sql_table);

#endif /* TABLE_FUNCTION_INCLUDED */

