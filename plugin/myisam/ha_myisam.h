/* Copyright (C) 2000-2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#ifndef PLUGIN_MYISAM_HA_MYISAM_H
#define PLUGIN_MYISAM_HA_MYISAM_H

#include <drizzled/cursor.h>
#include <drizzled/thr_lock.h>

/* class for the the myisam Cursor */

#include <plugin/myisam/myisam.h>

class ha_myisam: public drizzled::Cursor
{
  MI_INFO *file;
  char    *data_file_name, *index_file_name;
  bool can_enable_indexes;
  bool is_ordered;
  int repair(drizzled::Session *session, MI_CHECK &param, bool optimize);

 public:
  ha_myisam(drizzled::plugin::StorageEngine &engine,
            drizzled::TableShare &table_arg);
  ~ha_myisam() {}
  Cursor *clone(drizzled::memory::Root *mem_root);
  const char *index_type(uint32_t key_number);
  int index_init(uint32_t idx, bool sorted);
  int index_end();
  uint32_t checksum() const;

  int open(const char *name, int mode, uint32_t test_if_locked);
  int close(void);
  int doInsertRecord(unsigned char * buf);
  int doUpdateRecord(const unsigned char * old_data, unsigned char * new_data);
  int delete_row(const unsigned char * buf);
  int index_read_map(unsigned char *buf, const unsigned char *key, drizzled::key_part_map keypart_map,
                     enum drizzled::ha_rkey_function find_flag);
  int index_read_idx_map(unsigned char *buf, uint32_t index, const unsigned char *key,
                         drizzled::key_part_map keypart_map,
                         enum drizzled::ha_rkey_function find_flag);
  int index_read_last_map(unsigned char *buf, const unsigned char *key, drizzled::key_part_map keypart_map);
  int index_next(unsigned char * buf);
  int index_prev(unsigned char * buf);
  int index_first(unsigned char * buf);
  int index_last(unsigned char * buf);
  int index_next_same(unsigned char *buf, const unsigned char *key, uint32_t keylen);
  int rnd_init(bool scan);
  int rnd_next(unsigned char *buf);
  int rnd_pos(unsigned char * buf, unsigned char *pos);
  int restart_rnd_next(unsigned char *buf, unsigned char *pos);
  void position(const unsigned char *record);
  int info(uint);
  int extra(enum drizzled::ha_extra_function operation);
  int extra_opt(enum drizzled::ha_extra_function operation, uint32_t cache_size);
  int reset(void);
  int external_lock(drizzled::Session *session, int lock_type);
  int delete_all_rows(void);
  int disable_indexes(uint32_t mode);
  int enable_indexes(uint32_t mode);
  int indexes_are_disabled(void);
  void start_bulk_insert(drizzled::ha_rows rows);
  int end_bulk_insert();
  drizzled::ha_rows records_in_range(uint32_t inx, drizzled::key_range *min_key, drizzled::key_range *max_key);
  virtual void get_auto_increment(uint64_t offset, uint64_t increment,
                                  uint64_t nb_desired_values,
                                  uint64_t *first_value,
                                  uint64_t *nb_reserved_values);
  MI_INFO *file_ptr(void)
  {
    return file;
  }
  int read_range_first(const drizzled::key_range *start_key, const drizzled::key_range *end_key,
                       bool eq_range_arg, bool sorted);
  int read_range_next();
  int reset_auto_increment(uint64_t value);

  virtual bool isOrdered(void)
  {
    return false;
  }

private:
  drizzled::key_map keys_with_parts;
};

#endif /* PLUGIN_MYISAM_HA_MYISAM_H */
