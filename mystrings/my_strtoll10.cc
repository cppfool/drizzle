/* Copyright (C) 2003 MySQL AB

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

#include <m_string.h>
#include <errno.h>

#define MAX_NEGATIVE_NUMBER	((uint64_t) 0x8000000000000000LL)
#define INIT_CNT  9
#define LFACTOR   1000000000ULL
#define LFACTOR1  10000000000ULL
#define LFACTOR2  100000000000ULL

static uint64_t lfactor[9]=
{
  1L, 10L, 100L, 1000L, 10000L, 100000L, 1000000L, 10000000L, 100000000L
};

/*
  Convert a string to an to uint64_t integer value
  
  SYNOPSYS
    my_strtoll10()
      nptr     in       pointer to the string to be converted
      endptr   in/out   pointer to the end of the string/
                        pointer to the stop character
      error    out      returned error code
 
  DESCRIPTION
    This function takes the decimal representation of integer number
    from string nptr and converts it to an signed or unsigned
    int64_t value.
    Space characters and tab are ignored.
    A sign character might precede the digit characters. The number
    may have any number of pre-zero digits.

    The function stops reading the string nptr at the first character
    that is not a decimal digit. If endptr is not NULL then the function
    will not read characters after *endptr.
 
  RETURN VALUES
    Value of string as a signed/unsigned int64_t integer

    if no error and endptr != NULL, it will be set to point at the character
    after the number

    The error parameter contains information how things went:
    -1		Number was an ok negative number
    0	 	ok
    ERANGE	If the the value of the converted number exceeded the
	        maximum negative/uint64_t integer.
		In this case the return value is UINT64_MAX if value was
		positive and UINT64_MIN if value was negative.
    EDOM	If the string didn't contain any digits. In this case
    		the return value is 0.

    If endptr is not NULL the function will store the end pointer to
    the stop character here.
*/


int64_t my_strtoll10(const char *nptr, char **endptr, int *error)
{
  const char *s, *end, *start, *n_end, *true_end;
  char *dummy;
  unsigned char c;
  unsigned long i, j, k;
  uint64_t li;
  int negative;
  uint64_t cutoff, cutoff2, cutoff3;

  s= nptr;
  /* If fixed length string */
  if (endptr)
  {
    end= *endptr;
    while (s != end && (*s == ' ' || *s == '\t'))
      s++;
    if (s == end)
      goto no_conv;
  }
  else
  {
    endptr= &dummy;				/* Easier end test */
    while (*s == ' ' || *s == '\t')
      s++;
    if (!*s)
      goto no_conv;
    /* This number must be big to guard against a lot of pre-zeros */
    end= s+65535;				/* Can't be longer than this */
  }

  /* Check for a sign.	*/
  negative= 0;
  if (*s == '-')
  {
    *error= -1;					/* Mark as negative number */
    negative= 1;
    if (++s == end)
      goto no_conv;
    cutoff=  MAX_NEGATIVE_NUMBER / LFACTOR2;
    cutoff2= (MAX_NEGATIVE_NUMBER % LFACTOR2) / 100;
    cutoff3=  MAX_NEGATIVE_NUMBER % 100;
  }
  else
  {
    *error= 0;
    if (*s == '+')
    {
      if (++s == end)
	goto no_conv;
    }
    cutoff=  UINT64_MAX / LFACTOR2;
    cutoff2= UINT64_MAX % LFACTOR2 / 100;
    cutoff3=  UINT64_MAX % 100;
  }

  /* Handle case where we have a lot of pre-zero */
  if (*s == '0')
  {
    i= 0;
    do
    {
      if (++s == end)
	goto end_i;				/* Return 0 */
    }
    while (*s == '0');
    n_end= s+ INIT_CNT;
  }
  else
  {
    /* Read first digit to check that it's a valid number */
    if ((c= (*s-'0')) > 9)
      goto no_conv;
    i= c;
    n_end= ++s+ INIT_CNT-1;
  }

  /* Handle first 9 digits and store them in i */
  if (n_end > end)
    n_end= end;
  for (; s != n_end ; s++)
  {
    if ((c= (*s-'0')) > 9)
      goto end_i;
    i= i*10+c;
  }
  if (s == end)
    goto end_i;

  /* Handle next 9 digits and store them in j */
  j= 0;
  start= s;				/* Used to know how much to shift i */
  n_end= true_end= s + INIT_CNT;
  if (n_end > end)
    n_end= end;
  do
  {
    if ((c= (*s-'0')) > 9)
      goto end_i_and_j;
    j= j*10+c;
  } while (++s != n_end);
  if (s == end)
  {
    if (s != true_end)
      goto end_i_and_j;
    goto end3;
  }
  if ((c= (*s-'0')) > 9)
    goto end3;

  /* Handle the next 1 or 2 digits and store them in k */
  k=c;
  if (++s == end || (c= (*s-'0')) > 9)
    goto end4;
  k= k*10+c;
  *endptr= (char*) ++s;

  /* number string should have ended here */
  if (s != end && (c= (*s-'0')) <= 9)
    goto overflow;

  /* Check that we didn't get an overflow with the last digit */
  if (i > cutoff || (i == cutoff && ((j > cutoff2 || j == cutoff2) &&
                                     k > cutoff3)))
    goto overflow;
  li=i*LFACTOR2+ (uint64_t) j*100 + k;
  return (int64_t) li;

overflow:					/* *endptr is set here */
  *error= ERANGE;
  return negative ? INT64_MIN: INT64_MAX;

end_i:
  *endptr= (char*) s;
  return (negative ? ((int64_t) -(long) i) : (int64_t) i);

end_i_and_j:
  li= (uint64_t) i * lfactor[(unsigned int) (s-start)] + j;
  *endptr= (char*) s;
  return (negative ? -((int64_t) li) : (int64_t) li);

end3:
  li=(uint64_t) i*LFACTOR+ (uint64_t) j;
  *endptr= (char*) s;
  return (negative ? -((int64_t) li) : (int64_t) li);

end4:
  li=(uint64_t) i*LFACTOR1+ (uint64_t) j * 10 + k;
  *endptr= (char*) s;
  if (negative)
  {
   if (li > MAX_NEGATIVE_NUMBER)
     goto overflow;
   return -((int64_t) li);
  }
  return (int64_t) li;

no_conv:
  /* There was no number to convert.  */
  *error= EDOM;
  *endptr= (char *) nptr;
  return 0;
}
