/* Copyright (C) 2000 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

/*
  Cashing of files with only does (sequential) read or writes of fixed-
  length records. A read isn't allowed to go over file-length. A read is ok
  if it ends at file-length and next read can try to read after file-length
  (and get a EOF-error).
  Possibly use of asyncronic io.
  macros for read and writes for faster io.
  Used instead of FILE when reading or writing whole files.
  This code makes mf_rec_cache obsolete (currently only used by ISAM)
  One can change info->pos_in_file to a higher value to skip bytes in file if
  also info->read_pos is set to info->read_end.
  If called through open_cached_file(), then the temporary file will
  only be created if a write exeeds the file buffer or if one calls
  my_b_flush_io_cache().

  If one uses SEQ_READ_APPEND, then two buffers are allocated, one for
  reading and another for writing.  Reads are first done from disk and
  then done from the write buffer.  This is an efficient way to read
  from a log file when one is writing to it at the same time.
  For this to work, the file has to be opened in append mode!
  Note that when one uses SEQ_READ_APPEND, one MUST write using
  my_b_append !  This is needed because we need to lock the mutex
  every time we access the write buffer.

TODO:
  When one SEQ_READ_APPEND and we are reading and writing at the same time,
  each time the write buffer gets full and it's written to disk, we will
  always do a disk read to read a part of the buffer from disk to the
  read buffer.
  This should be fixed so that when we do a my_b_flush_io_cache() and
  we have been reading the write buffer, we should transfer the rest of the
  write buffer to the read buffer before we start to reuse it.
*/

#include "config.h"

#include "drizzled/internal/my_sys.h"
#include "drizzled/internal/m_string.h"
#ifdef HAVE_AIOWAIT
#include "drizzled/error.h"
#include "drizzled/internal/aio_result.h"
static void my_aiowait(my_aio_result *result);
#endif
#include "drizzled/internal/iocache.h"
#include <errno.h>
#include <drizzled/util/test.h>
#include <stdlib.h>
#include <algorithm>

using namespace std;

namespace drizzled
{
namespace internal
{

static int _my_b_read(register IO_CACHE *info, unsigned char *Buffer, size_t Count);
static int _my_b_write(register IO_CACHE *info, const unsigned char *Buffer, size_t Count);

/**
 * @brief
 *   Lock appends for the IO_CACHE if required (need_append_buffer_lock)   
 */
inline
static void lock_append_buffer(register IO_CACHE *, int )
{
}

/**
 * @brief
 *   Unlock appends for the IO_CACHE if required (need_append_buffer_lock)   
 */
inline
static void unlock_append_buffer(register IO_CACHE *, int )
{
}

/**
 * @brief
 *   Round up to the nearest IO_SIZE boundary 
 */
inline
static size_t io_round_up(size_t x)
{
  return ((x+IO_SIZE-1) & ~(IO_SIZE-1));
}

/**
 * @brief
 *   Round down to the nearest IO_SIZE boundary   
 */
inline
static size_t io_round_dn(size_t x)
{
  return (x & ~(IO_SIZE-1));
}


/**
 * @brief 
 *   Setup internal pointers inside IO_CACHE
 * 
 * @details
 *   This is called on automatically on init or reinit of IO_CACHE
 *   It must be called externally if one moves or copies an IO_CACHE object.
 * 
 * @param info Cache handler to setup
 */
void setup_io_cache(IO_CACHE* info)
{
  /* Ensure that my_b_tell() and my_b_bytes_in_cache works */
  if (info->type == WRITE_CACHE)
  {
    info->current_pos= &info->write_pos;
    info->current_end= &info->write_end;
  }
  else
  {
    info->current_pos= &info->read_pos;
    info->current_end= &info->read_end;
  }
}


static void
init_functions(IO_CACHE* info)
{
  enum cache_type type= info->type;
  switch (type) {
  case READ_NET:
    /*
      Must be initialized by the caller. The problem is that
      _my_b_net_read has to be defined in sql directory because of
      the dependency on THD, and therefore cannot be visible to
      programs that link against mysys but know nothing about THD, such
      as myisamchk
    */
    break;
  default:
    info->read_function = _my_b_read;
    info->write_function = _my_b_write;
  }

  setup_io_cache(info);
}

/**
 * @brief
 *   Initialize an IO_CACHE object
 *
 * @param info Cache handler to initialize
 * @param file File that should be associated with the handler
 *                 If == -1 then real_open_cached_file() will be called when it's time to open file.
 * @param cachesize Size of buffer to allocate for read/write
 *                      If == 0 then use my_default_record_cache_size
 * @param type Type of cache
 * @param seek_offset Where cache should start reading/writing
 * @param use_async_io Set to 1 if we should use async_io (if avaiable)
 * @param cache_myflags Bitmap of different flags
                            MY_WME | MY_FAE | MY_NABP | MY_FNABP | MY_DONT_CHECK_FILESIZE
 * 
 * @retval 0 success
 * @retval # error
 */
int init_io_cache(IO_CACHE *info, int file, size_t cachesize,
		  enum cache_type type, my_off_t seek_offset,
		  bool use_async_io, myf cache_myflags)
{
  size_t min_cache;
  off_t pos;
  my_off_t end_of_file= ~(my_off_t) 0;

  info->file= file;
  info->type= TYPE_NOT_SET;	    /* Don't set it until mutex are created */
  info->pos_in_file= seek_offset;
  info->pre_close = info->pre_read = info->post_read = 0;
  info->arg = 0;
  info->alloced_buffer = 0;
  info->buffer=0;
  info->seek_not_done= 0;

  if (file >= 0)
  {
    pos= lseek(file, 0, SEEK_CUR);
    if ((pos == MY_FILEPOS_ERROR) && (errno == ESPIPE))
    {
      /*
         This kind of object doesn't support seek() or tell(). Don't set a
         flag that will make us again try to seek() later and fail.
      */
      info->seek_not_done= 0;
      /*
        Additionally, if we're supposed to start somewhere other than the
        the beginning of whatever this file is, then somebody made a bad
        assumption.
      */
      assert(seek_offset == 0);
    }
    else
      info->seek_not_done= test(seek_offset != (my_off_t)pos);
  }

  if (!cachesize && !(cachesize= my_default_record_cache_size))
    return(1);				/* No cache requested */
  min_cache=use_async_io ? IO_SIZE*4 : IO_SIZE*2;
  if (type == READ_CACHE)
  {						/* Assume file isn't growing */
    if (!(cache_myflags & MY_DONT_CHECK_FILESIZE))
    {
      /* Calculate end of file to avoid allocating oversized buffers */
      end_of_file=lseek(file,0L,SEEK_END);
      /* Need to reset seek_not_done now that we just did a seek. */
      info->seek_not_done= end_of_file == seek_offset ? 0 : 1;
      if (end_of_file < seek_offset)
	end_of_file=seek_offset;
      /* Trim cache size if the file is very small */
      if ((my_off_t) cachesize > end_of_file-seek_offset+IO_SIZE*2-1)
      {
	cachesize= (size_t) (end_of_file-seek_offset)+IO_SIZE*2-1;
	use_async_io=0;				/* No need to use async */
      }
    }
  }
  cache_myflags &= ~MY_DONT_CHECK_FILESIZE;
  if (type != READ_NET && type != WRITE_NET)
  {
    /* Retry allocating memory in smaller blocks until we get one */
    cachesize= ((cachesize + min_cache-1) & ~(min_cache-1));
    for (;;)
    {
      size_t buffer_block;
      if (cachesize < min_cache)
	cachesize = min_cache;
      buffer_block= cachesize;
      if ((info->buffer=
	   (unsigned char*) malloc(buffer_block)) != 0)
      {
	info->write_buffer=info->buffer;
	info->alloced_buffer= true;
	break;					/* Enough memory found */
      }
      if (cachesize == min_cache)
	return(2);				/* Can't alloc cache */
      /* Try with less memory */
      cachesize= (cachesize*3/4 & ~(min_cache-1));
    }
  }

  info->read_length=info->buffer_length=cachesize;
  info->myflags=cache_myflags & ~(MY_NABP | MY_FNABP);
  info->request_pos= info->read_pos= info->write_pos = info->buffer;

  if (type == WRITE_CACHE)
    info->write_end=
      info->buffer+info->buffer_length- (seek_offset & (IO_SIZE-1));
  else
    info->read_end=info->buffer;		/* Nothing in cache */

  /* End_of_file may be changed by user later */
  info->end_of_file= end_of_file;
  info->error=0;
  info->type= type;
  init_functions(info);
#ifdef HAVE_AIOWAIT
  if (use_async_io && ! my_disable_async_io)
  {
    info->read_length/=2;
    info->read_function=_my_b_async_read;
  }
  info->inited=info->aio_result.pending=0;
#endif
  return(0);
}						/* init_io_cache */

	/* Wait until current request is ready */

#ifdef HAVE_AIOWAIT
static void my_aiowait(my_aio_result *result)
{
  if (result->pending)
  {
    struct aio_result_t *tmp;
    for (;;)
    {
      if ((int) (tmp=aiowait((struct timeval *) 0)) == -1)
      {
	if (errno == EINTR)
	  continue;
	result->pending=0;			/* Assume everythings is ok */
	break;
      }
      ((my_aio_result*) tmp)->pending=0;
      if ((my_aio_result*) tmp == result)
	break;
    }
  }
  return;
}
#endif

/**
 * @brief 
 *   Reset the cache
 * 
 * @detail
 *   Use this to reset cache to re-start reading or to change the type 
 *   between READ_CACHE <-> WRITE_CACHE
 *   If we are doing a reinit of a cache where we have the start of the file
 *   in the cache, we are reusing this memory without flushing it to disk.
 */
bool reinit_io_cache(IO_CACHE *info, enum cache_type type,
			my_off_t seek_offset,
			bool use_async_io,
			bool clear_cache)
{
  /* One can't do reinit with the following types */
  assert(type != READ_NET && info->type != READ_NET &&
	      type != WRITE_NET && info->type != WRITE_NET);

  /* If the whole file is in memory, avoid flushing to disk */
  if (! clear_cache &&
      seek_offset >= info->pos_in_file &&
      seek_offset <= my_b_tell(info))
  {
    /* Reuse current buffer without flushing it to disk */
    unsigned char *pos;
    if (info->type == WRITE_CACHE && type == READ_CACHE)
    {
      info->read_end=info->write_pos;
      info->end_of_file=my_b_tell(info);
      /*
        Trigger a new seek only if we have a valid
        file handle.
      */
      info->seek_not_done= (info->file != -1);
    }
    else if (type == WRITE_CACHE)
    {
      if (info->type == READ_CACHE)
      {
	info->write_end=info->write_buffer+info->buffer_length;
	info->seek_not_done=1;
      }
      info->end_of_file = ~(my_off_t) 0;
    }
    pos=info->request_pos+(seek_offset-info->pos_in_file);
    if (type == WRITE_CACHE)
      info->write_pos=pos;
    else
      info->read_pos= pos;
#ifdef HAVE_AIOWAIT
    my_aiowait(&info->aio_result);		/* Wait for outstanding req */
#endif
  }
  else
  {
    /*
      If we change from WRITE_CACHE to READ_CACHE, assume that everything
      after the current positions should be ignored
    */
    if (info->type == WRITE_CACHE && type == READ_CACHE)
      info->end_of_file=my_b_tell(info);
    /* flush cache if we want to reuse it */
    if (!clear_cache && my_b_flush_io_cache(info,1))
      return(1);
    info->pos_in_file=seek_offset;
    /* Better to do always do a seek */
    info->seek_not_done=1;
    info->request_pos=info->read_pos=info->write_pos=info->buffer;
    if (type == READ_CACHE)
    {
      info->read_end=info->buffer;		/* Nothing in cache */
    }
    else
    {
      info->write_end=(info->buffer + info->buffer_length -
		       (seek_offset & (IO_SIZE-1)));
      info->end_of_file= ~(my_off_t) 0;
    }
  }
  info->type=type;
  info->error=0;
  init_functions(info);

#ifdef HAVE_AIOWAIT
  if (use_async_io && ! my_disable_async_io &&
      ((uint32_t) info->buffer_length <
       (uint32_t) (info->end_of_file - seek_offset)))
  {
    info->read_length=info->buffer_length/2;
    info->read_function=_my_b_async_read;
  }
  info->inited=0;
#else
  (void)use_async_io;
#endif
  return(0);
} /* reinit_io_cache */

/**
 * @brief 
 *   Read buffered.
 * 
 * @detail
 *   This function is only called from the my_b_read() macro when there
 *   aren't enough characters in the buffer to satisfy the request.
 *
 * WARNING
 *   When changing this function, be careful with handling file offsets
 *   (end-of_file, pos_in_file). Do not cast them to possibly smaller
 *   types than my_off_t unless you can be sure that their value fits.
 *   Same applies to differences of file offsets.
 *
 * @param info IO_CACHE pointer @param Buffer Buffer to retrieve count bytes
 * from file @param Count Number of bytes to read into Buffer
 * 
 * @retval 0 We succeeded in reading all data
 * @retval 1 Error: can't read requested characters
 */
static int _my_b_read(register IO_CACHE *info, unsigned char *Buffer, size_t Count)
{
  size_t length,diff_length,left_length, max_length;
  my_off_t pos_in_file;

  if ((left_length= (size_t) (info->read_end-info->read_pos)))
  {
    assert(Count >= left_length);	/* User is not using my_b_read() */
    memcpy(Buffer,info->read_pos, left_length);
    Buffer+=left_length;
    Count-=left_length;
  }

  /* pos_in_file always point on where info->buffer was read */
  pos_in_file=info->pos_in_file+ (size_t) (info->read_end - info->buffer);

  /*
    Whenever a function which operates on IO_CACHE flushes/writes
    some part of the IO_CACHE to disk it will set the property
    "seek_not_done" to indicate this to other functions operating
    on the IO_CACHE.
  */
  if (info->seek_not_done)
  {
    if ((lseek(info->file,pos_in_file,SEEK_SET) != MY_FILEPOS_ERROR))
    {
      /* No error, reset seek_not_done flag. */
      info->seek_not_done= 0;
    }
    else
    {
      /*
        If the seek failed and the error number is ESPIPE, it is because
        info->file is a pipe or socket or FIFO.  We never should have tried
        to seek on that.  See Bugs#25807 and #22828 for more info.
      */
      assert(errno != ESPIPE);
      info->error= -1;
      return(1);
    }
  }

  diff_length= (size_t) (pos_in_file & (IO_SIZE-1));
  if (Count >= (size_t) (IO_SIZE+(IO_SIZE-diff_length)))
  {					/* Fill first intern buffer */
    size_t read_length;
    if (info->end_of_file <= pos_in_file)
    {					/* End of file */
      info->error= (int) left_length;
      return(1);
    }
    length=(Count & (size_t) ~(IO_SIZE-1))-diff_length;
    if ((read_length= my_read(info->file,Buffer, length, info->myflags))
	!= length)
    {
      info->error= (read_length == (size_t) -1 ? -1 :
		    (int) (read_length+left_length));
      return(1);
    }
    Count-=length;
    Buffer+=length;
    pos_in_file+=length;
    left_length+=length;
    diff_length=0;
  }

  max_length= info->read_length-diff_length;
  if (info->type != READ_FIFO &&
      max_length > (info->end_of_file - pos_in_file))
    max_length= (size_t) (info->end_of_file - pos_in_file);
  if (!max_length)
  {
    if (Count)
    {
      info->error= static_cast<int>(left_length);	/* We only got this many char */
      return(1);
    }
    length=0;				/* Didn't read any chars */
  }
  else if ((length= my_read(info->file,info->buffer, max_length,
                            info->myflags)) < Count ||
	   length == (size_t) -1)
  {
    if (length != (size_t) -1)
      memcpy(Buffer, info->buffer, length);
    info->pos_in_file= pos_in_file;
    info->error= length == (size_t) -1 ? -1 : (int) (length+left_length);
    info->read_pos=info->read_end=info->buffer;
    return(1);
  }
  info->read_pos=info->buffer+Count;
  info->read_end=info->buffer+length;
  info->pos_in_file=pos_in_file;
  memcpy(Buffer, info->buffer, Count);
  return(0);
}


#ifdef HAVE_AIOWAIT

/**
 * @brief
 *   Read from the IO_CACHE into a buffer and feed asynchronously from disk when needed.
 *
 * @param info IO_CACHE pointer
 * @param Buffer Buffer to retrieve count bytes from file
 * @param Count Number of bytes to read into Buffer
 * 
 * @retval -1 An error has occurred; errno is set.
 * @retval 0 Success
 * @retval 1 An error has occurred; IO_CACHE to error state.
 */
int _my_b_async_read(register IO_CACHE *info, unsigned char *Buffer, size_t Count)
{
  size_t length,read_length,diff_length,left_length,use_length,org_Count;
  size_t max_length;
  my_off_t next_pos_in_file;
  unsigned char *read_buffer;

  memcpy(Buffer,info->read_pos,
	 (left_length= (size_t) (info->read_end-info->read_pos)));
  Buffer+=left_length;
  org_Count=Count;
  Count-=left_length;

  if (info->inited)
  {						/* wait for read block */
    info->inited=0;				/* No more block to read */
    my_aiowait(&info->aio_result);		/* Wait for outstanding req */
    if (info->aio_result.result.aio_errno)
    {
      if (info->myflags & MY_WME)
	my_error(EE_READ, MYF(ME_BELL+ME_WAITTANG),
		 my_filename(info->file),
		 info->aio_result.result.aio_errno);
      errno=info->aio_result.result.aio_errno;
      info->error= -1;
      return(1);
    }
    if (! (read_length= (size_t) info->aio_result.result.aio_return) ||
	read_length == (size_t) -1)
    {
      errno=0;				/* For testing */
      info->error= (read_length == (size_t) -1 ? -1 :
		    (int) (read_length+left_length));
      return(1);
    }
    info->pos_in_file+= (size_t) (info->read_end - info->request_pos);

    if (info->request_pos != info->buffer)
      info->request_pos=info->buffer;
    else
      info->request_pos=info->buffer+info->read_length;
    info->read_pos=info->request_pos;
    next_pos_in_file=info->aio_read_pos+read_length;

	/* Check if pos_in_file is changed
	   (_ni_read_cache may have skipped some bytes) */

    if (info->aio_read_pos < info->pos_in_file)
    {						/* Fix if skipped bytes */
      if (info->aio_read_pos + read_length < info->pos_in_file)
      {
	read_length=0;				/* Skip block */
	next_pos_in_file=info->pos_in_file;
      }
      else
      {
	my_off_t offset= (info->pos_in_file - info->aio_read_pos);
	info->pos_in_file=info->aio_read_pos; /* Whe are here */
	info->read_pos=info->request_pos+offset;
	read_length-=offset;			/* Bytes left from read_pos */
      }
    }
	/* Copy found bytes to buffer */
    length=min(Count,read_length);
    memcpy(Buffer,info->read_pos,(size_t) length);
    Buffer+=length;
    Count-=length;
    left_length+=length;
    info->read_end=info->rc_pos+read_length;
    info->read_pos+=length;
  }
  else
    next_pos_in_file=(info->pos_in_file+ (size_t)
		      (info->read_end - info->request_pos));

	/* If reading large blocks, or first read or read with skip */
  if (Count)
  {
    if (next_pos_in_file == info->end_of_file)
    {
      info->error=(int) (read_length+left_length);
      return 1;
    }

    if (lseek(info->file,next_pos_in_file,SEEK_SET) == MY_FILEPOS_ERROR)
    {
      info->error= -1;
      return (1);
    }

    read_length=IO_SIZE*2- (size_t) (next_pos_in_file & (IO_SIZE-1));
    if (Count < read_length)
    {					/* Small block, read to cache */
      if ((read_length=my_read(info->file,info->request_pos,
			       read_length, info->myflags)) == (size_t) -1)
        return info->error= -1;
      use_length=min(Count,read_length);
      memcpy(Buffer,info->request_pos,(size_t) use_length);
      info->read_pos=info->request_pos+Count;
      info->read_end=info->request_pos+read_length;
      info->pos_in_file=next_pos_in_file;	/* Start of block in cache */
      next_pos_in_file+=read_length;

      if (Count != use_length)
      {					/* Didn't find hole block */
	if (info->myflags & (MY_WME | MY_FAE | MY_FNABP) && Count != org_Count)
	  my_error(EE_EOFERR, MYF(ME_BELL+ME_WAITTANG),
		   my_filename(info->file),errno);
	info->error=(int) (read_length+left_length);
	return 1;
      }
    }
    else
    {						/* Big block, don't cache it */
      if ((read_length= my_read(info->file,Buffer, Count,info->myflags))
	  != Count)
      {
	info->error= read_length == (size_t) -1 ? -1 : read_length+left_length;
	return 1;
      }
      info->read_pos=info->read_end=info->request_pos;
      info->pos_in_file=(next_pos_in_file+=Count);
    }
  }

  /* Read next block with asyncronic io */
  diff_length=(next_pos_in_file & (IO_SIZE-1));
  max_length= info->read_length - diff_length;
  if (max_length > info->end_of_file - next_pos_in_file)
    max_length= (size_t) (info->end_of_file - next_pos_in_file);

  if (info->request_pos != info->buffer)
    read_buffer=info->buffer;
  else
    read_buffer=info->buffer+info->read_length;
  info->aio_read_pos=next_pos_in_file;
  if (max_length)
  {
    info->aio_result.result.aio_errno=AIO_INPROGRESS;	/* Marker for test */
    if (aioread(info->file,read_buffer, max_length,
		(my_off_t) next_pos_in_file,SEEK_SET,
		&info->aio_result.result))
    {						/* Skip async io */
      errno=errno;
      if (info->request_pos != info->buffer)
      {
        memmove(info->buffer, info->request_pos,
                (size_t) (info->read_end - info->read_pos));
	info->request_pos=info->buffer;
	info->read_pos-=info->read_length;
	info->read_end-=info->read_length;
      }
      info->read_length=info->buffer_length;	/* Use hole buffer */
      info->read_function=_my_b_read;		/* Use normal IO_READ next */
    }
    else
      info->inited=info->aio_result.pending=1;
  }
  return 0;					/* Block read, async in use */
} /* _my_b_async_read */
#endif


/**
 * @brief
 *   Read one byte when buffer is empty
 */
int _my_b_get(IO_CACHE *info)
{
  unsigned char buff;
  IO_CACHE_CALLBACK pre_read,post_read;
  if ((pre_read = info->pre_read))
    (*pre_read)(info);
  if ((*(info)->read_function)(info,&buff,1))
    return my_b_EOF;
  if ((post_read = info->post_read))
    (*post_read)(info);
  return (int) (unsigned char) buff;
}

/**
 * @brief
 *   Write a byte buffer to IO_CACHE and flush to disk if IO_CACHE is full.
 *
 * @retval -1 On error; errno contains error code.
 * @retval 0 On success
 * @retval 1 On error on write
 */
int _my_b_write(register IO_CACHE *info, const unsigned char *Buffer, size_t Count)
{
  size_t rest_length,length;

  if (info->pos_in_file+info->buffer_length > info->end_of_file)
  {
    errno=errno=EFBIG;
    return info->error = -1;
  }

  rest_length= (size_t) (info->write_end - info->write_pos);
  memcpy(info->write_pos,Buffer,(size_t) rest_length);
  Buffer+=rest_length;
  Count-=rest_length;
  info->write_pos+=rest_length;

  if (my_b_flush_io_cache(info,1))
    return 1;
  if (Count >= IO_SIZE)
  {					/* Fill first intern buffer */
    length=Count & (size_t) ~(IO_SIZE-1);
    if (info->seek_not_done)
    {
      /*
        Whenever a function which operates on IO_CACHE flushes/writes
        some part of the IO_CACHE to disk it will set the property
        "seek_not_done" to indicate this to other functions operating
        on the IO_CACHE.
      */
      if (lseek(info->file,info->pos_in_file,SEEK_SET))
      {
        info->error= -1;
        return (1);
      }
      info->seek_not_done=0;
    }
    if (my_write(info->file, Buffer, length, info->myflags | MY_NABP))
      return info->error= -1;

    Count-=length;
    Buffer+=length;
    info->pos_in_file+=length;
  }
  memcpy(info->write_pos,Buffer,(size_t) Count);
  info->write_pos+=Count;
  return 0;
}

/**
 * @brief
 *   Write a block to disk where part of the data may be inside the record buffer.
 *   As all write calls to the data goes through the cache,
 *   we will never get a seek over the end of the buffer.
 */
int my_block_write(register IO_CACHE *info, const unsigned char *Buffer, size_t Count,
		   my_off_t pos)
{
  size_t length;
  int error=0;

  if (pos < info->pos_in_file)
  {
    /* Of no overlap, write everything without buffering */
    if (pos + Count <= info->pos_in_file)
      return (pwrite(info->file, Buffer, Count, pos) == 0);
    /* Write the part of the block that is before buffer */
    length= (uint32_t) (info->pos_in_file - pos);
    if (pwrite(info->file, Buffer, length, pos) == 0)
      info->error= error= -1;
    Buffer+=length;
    pos+=  length;
    Count-= length;
  }

  /* Check if we want to write inside the used part of the buffer.*/
  length= (size_t) (info->write_end - info->buffer);
  if (pos < info->pos_in_file + length)
  {
    size_t offset= (size_t) (pos - info->pos_in_file);
    length-=offset;
    if (length > Count)
      length=Count;
    memcpy(info->buffer+offset, Buffer, length);
    Buffer+=length;
    Count-= length;
    /* Fix length of buffer if the new data was larger */
    if (info->buffer+length > info->write_pos)
      info->write_pos=info->buffer+length;
    if (!Count)
      return (error);
  }
  /* Write at the end of the current buffer; This is the normal case */
  if (_my_b_write(info, Buffer, Count))
    error= -1;
  return error;
}

/**
 * @brief
 *   Flush write cache 
 */
int my_b_flush_io_cache(IO_CACHE *info, int need_append_buffer_lock)
{
  size_t length;
  bool append_cache= false;
  my_off_t pos_in_file;

  if (info->type == WRITE_CACHE || append_cache)
  {
    if (info->file == -1)
    {
      if (real_open_cached_file(info))
	return((info->error= -1));
    }
    lock_append_buffer(info, need_append_buffer_lock);

    if ((length=(size_t) (info->write_pos - info->write_buffer)))
    {
      pos_in_file=info->pos_in_file;
      /*
	If we have append cache, we always open the file with
	O_APPEND which moves the pos to EOF automatically on every write
      */
      if (!append_cache && info->seek_not_done)
      {					/* File touched, do seek */
	if (lseek(info->file,pos_in_file,SEEK_SET) == MY_FILEPOS_ERROR)
	{
	  unlock_append_buffer(info, need_append_buffer_lock);
	  return((info->error= -1));
	}
	if (!append_cache)
	  info->seek_not_done=0;
      }
      if (!append_cache)
	info->pos_in_file+=length;
      info->write_end= (info->write_buffer+info->buffer_length-
			((pos_in_file+length) & (IO_SIZE-1)));

      if (my_write(info->file,info->write_buffer,length,
		   info->myflags | MY_NABP))
	info->error= -1;
      else
	info->error= 0;
      if (!append_cache)
      {
        set_if_bigger(info->end_of_file,(pos_in_file+length));
      }
      else
      {
	info->end_of_file+=(info->write_pos-info->append_read_pos);
	my_off_t tell_ret= lseek(info->file, 0, SEEK_CUR);
	assert(info->end_of_file == tell_ret);
      }

      info->append_read_pos=info->write_pos=info->write_buffer;
      unlock_append_buffer(info, need_append_buffer_lock);
      return(info->error);
    }
  }
#ifdef HAVE_AIOWAIT
  else if (info->type != READ_NET)
  {
    my_aiowait(&info->aio_result);		/* Wait for outstanding req */
    info->inited=0;
  }
#endif
  unlock_append_buffer(info, need_append_buffer_lock);
  return(0);
}

/**
 * @brief
 *   Free an IO_CACHE object
 * 
 * @detail
 *   It's currently safe to call this if one has called init_io_cache()
 *   on the 'info' object, even if init_io_cache() failed.
 *   This function is also safe to call twice with the same handle.
 * 
 * @param info IO_CACHE Handle to free
 * 
 * @retval 0 ok
 * @retval # Error
 */
int end_io_cache(IO_CACHE *info)
{
  int error=0;
  IO_CACHE_CALLBACK pre_close;

  if ((pre_close=info->pre_close))
  {
    (*pre_close)(info);
    info->pre_close= 0;
  }
  if (info->alloced_buffer)
  {
    info->alloced_buffer=0;
    if (info->file != -1)			/* File doesn't exist */
      error= my_b_flush_io_cache(info,1);
    free((unsigned char*) info->buffer);
    info->buffer=info->read_pos=(unsigned char*) 0;
  }

  return(error);
} /* end_io_cache */

} /* namespace internal */
} /* namespace drizzled */
