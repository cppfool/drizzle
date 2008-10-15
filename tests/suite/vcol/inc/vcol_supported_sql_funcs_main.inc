################################################################################
# inc/vcol_supported_sql_funcs_main.inc                                        #
#                                                                              #
# Purpose:                                                                     #
#  Tests frame for allowed sql functions                                       #
#                                                                              #
#                                                                              #
#------------------------------------------------------------------------------#
# Original Author: Andrey Zhakov                                               #
# Original Date: 2008-08-31                                                    #
# Change Author:                                                               #
# Change Date:                                                                 #
# Change:                                                                      #
################################################################################

--echo #
--echo # NUMERIC FUNCTIONS
--echo #

--echo # ABS()
let $cols = a int, b virtual int as (abs(a));
let $values1 = -1, default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # ACOS()
let $cols = a double, b virtual double as (format(acos(a),6));
let $values1 = 1, default;
let $values2 = 1.0001,default;
let $values3 = 0,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # ASIN()
let $cols = a double, b virtual double as (format(asin(a),6));
let $values1 = 0.2, default;
let $values2 = 1.0001,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo #ATAN
let $cols = a double, b double, c virtual double as (format(atan(a,b),6));
let $values1 = -2,2,default;
let $values2 = format(PI(),6),0,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

let $cols = a double, c virtual double as (format(atan(a),6));
let $values1 = -2,default;
let $values2 = format(PI(),6),default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # ATAN2
let $cols = a double, b double, c virtual double as (format(atan2(a,b),6));
let $values1 = -2,2,default;
let $values2 = format(PI(),6),0,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # CEIL()
let $cols = a double, b virtual int as (ceil(a));
let $values1 = 1.23,default;
let $values2 = -1.23,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # CONV()
let $cols = a varchar(10), b int, c int, d virtual varchar(10) as (conv(a,b,c));
let $values1 = 'a',16,2,default;
let $values2 = '6e',18,8,default;
let $values3 = -17,10,-18,default;
let $values4 = 10+'10'+'10'+0xa,10,10,default;
let $rows = 4;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # COS()
let $cols = a double, b virtual double as (format(cos(a),6));
let $values1 = format(PI(),6),default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # COT()
let $cols = a double, b virtual double as (format(cot(a),6));
let $values1 = 12,default;
let $values2 = 0,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DEGREES()
let $cols = a double, b virtual double as (format(degrees(a),6));
let $values1 = format(PI(),6),default;
let $values2 = format(PI()/2,6),default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # /
let $cols = a double, b virtual double as (a/2);
let $values1 = 2,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # EXP()
let $cols = a double, b virtual double as (format(exp(a),6));
let $values1 = 2,default;
let $values2 = -2,default;
let $values3 = 0,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # FLOOR()
let $cols = a double, b virtual int as (floor(a));
let $values1 = 1.23,default;
let $values2 = -1.23,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LN()
let $cols = a double, b virtual double as (format(ln(a),6));
let $values1 = 2,default;
let $values2 = -2,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LOG()
let $cols = a double, b double, c virtual double as (format(log(a,b),6));
let $values1 = 2,65536,default;
let $values2 = 10,100,default;
let $values3 = 1,100,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

let $cols = a double, b virtual double as (format(log(a),6));
let $values1 = 2,default;
let $values2 = -2,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LOG2()
let $cols = a double, b virtual double as (format(log2(a),6));
let $values1 = 65536,default;
let $values2 = -100,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LOG10()
let $cols = a double, b virtual double as (format(log10(a),6));
let $values1 = 2,default;
let $values2 = 100,default;
let $values3 = -100,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # -
let $cols = a double, b virtual double as (a-1);
let $values1 = 2,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MOD()
let $cols = a int, b virtual int as (mod(a,10));
let $values1 = 1,default;
let $values2 = 11,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # %
let $cols = a int, b virtual int as (a % 10);
let $values1 = 1,default;
let $values2 = 11,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # OCT()
let $cols = a double, b virtual varchar(10) as (oct(a));
let $values1 = 12,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # PI()
let $cols = a double, b virtual double as (format(PI()*a*a,6));
let $values1 = 1,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # +
let $cols = a int, b virtual int as (a+1);
let $values1 = 1,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # POW, POWER
let $cols = a int, b virtual int as (pow(a,2)), c virtual int as (power(a,2));
let $values1 = 1,default,default;
let $values2 = 2,default,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # RADIANS()
let $cols = a double, b virtual double as (format(radians(a),6));
let $values1 = 90,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # ROUND()
let $cols = a double, b virtual int as (round(a));
let $values1 = -1.23,default;
let $values2 = -1.58,default;
let $values3 = 1.58,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

let $cols = a double, b double, c virtual int as (round(a,b));
let $values1 = 1.298,1,default;
let $values2 = 1.298,0,default;
let $values3 = 23.298,-1,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SIGN()
let $cols = a double, b virtual int as (sign(a));
let $values1 = -32,default;
let $values2 = 0,default;
let $values3 = 234,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SIN()
let $cols = a double, b virtual double as (format(sin(a),6));
let $values1 = format(PI()/2,6),default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SQRT()
let $cols = a double, b virtual double as (format(sqrt(a),6));
let $values1 = 4,default;
let $values2 = 20,default;
let $values3 = -16,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TAN()
let $cols = a double, b virtual double as (format(tan(a),6));
let $values1 = format(PI(),6),default;
let $values2 = format(PI()+1,6),default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # *
let $cols = a double, b virtual double as (a*3);
let $values1 = 0,default;
let $values2 = 1,default;
let $values3 = 2,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TRUNCATE()
let $cols = a double, b virtual double as (truncate(a,4));
let $values1 = 1.223,default;
let $values2 = 1.999,default;
let $values3 = 1.999,default;
let $values4 = 122,default;
let $rows = 4;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # Unary -
let $cols = a double, b virtual double as (-a);
let $values1 = 1,default;
let $values2 = -1,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo #
--echo # STRING FUNCTIONS
--echo #

--echo # ASCII()
let $cols = a char(2), b virtual int as (ascii(a));
let $values1 = '2',default;
let $values2 = 2,default;
let $values3 = 'dx',default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # BIN()
let $cols = a int, b virtual varchar(10) as (bin(a));
let $values1 = 12,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # BIT_LENGTH()
let $cols = a varchar(10), b virtual int as (bit_length(a));
let $values1 = 'text',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # CHAR_LENGTH()
let $cols = a varchar(10), b virtual int as (char_length(a));
let $values1 = 'text',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # CHAR()
let $cols = a int, b int, c virtual varbinary(10) as (char(a,b));
let $values1 = 77,121,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # CHARACTER_LENGTH()
let $cols = a varchar(10), b virtual int as (character_length(a));
let $values1 = 'text',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # CONCAT_WS()
let $cols = a varchar(10), b varchar(10), c virtual varchar(20) as (concat_ws(',',a,b));
let $values1 = 'value1','value2',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # CONCAT()
let $cols = a varchar(10), b varchar(10), c virtual varchar(20) as (concat(a,',',b));
let $values1 = 'value1','value2',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # ELT()
let $cols = a varchar(10), b varchar(10), c int, d virtual varchar(10) as (elt(c,a,b));
let $values1 = 'value1','value2',1,default;
let $values2 = 'value1','value2',2,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # EXPORT_SET()
let $cols = a int, b virtual varchar(10) as (export_set(a,'1','0','',10));
let $values1 = 6,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # FIELD()
let $cols = a varchar(10), b varchar(10), c virtual int as (field('aa',a,b));
let $values1 = 'aa','bb',default;
let $values2 = 'bb','aa',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # FIND_IN_SET()
let $cols = a varchar(10), b varchar(10), c virtual int as (find_in_set(a,b));
let $values1 = 'aa','aa,bb,cc',default;
let $values2 = 'aa','bb,aa,cc',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # FORMAT()
let $cols = a double, b virtual varchar(20) as (format(a,2));
let $values1 = 12332.123456,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # HEX()
let $cols = a double, b virtual varchar(10) as (hex(a));
let $values1 = 17,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

let $cols = a varchar(10), b virtual varchar(10) as (hex(a));
let $values1 = 'abc',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # INSERT()
let $cols = a varchar(10), b varchar(10), c virtual varchar(20) as (insert(a,length(a),length(b),b));
let $values1 = 'start,','end',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # INSTR()
let $cols = a varchar(10), b varchar(10), c virtual int as (instr(a,b));
let $values1 = 'foobarbar,','bar',default;
let $values2 = 'xbar,','foobar',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LCASE()
let $cols = a varchar(10), b virtual varchar(10) as (lcase(a));
let $values1 = 'MySQL',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LEFT()
let $cols = a varchar(10), b virtual varchar(5) as (left(a,5));
let $values1 = 'foobarbar',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LENGTH()
let $cols = a varchar(10), b virtual int as (length(a));
let $values1 = 'text',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LIKE
let $cols = a varchar(10), b virtual int as (a like 'H%o');
let $values1 = 'Hello',default;
let $values2 = 'MySQL',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LOCATE()
let $cols = a varchar(10), b virtual varchar(10) as (locate('bar',a));
let $values1 = 'foobarbar',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LOWER()
let $cols = a varchar(10), b virtual varchar(10) as (lower(a));
let $values1 = 'MySQL',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LPAD()
let $cols = a varchar(10), b virtual varchar(10) as (lpad(a,4,' '));
let $values1 = 'MySQL',default;
let $values2 = 'M',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LTRIM()
let $cols = a varchar(10), b virtual varchar(10) as (ltrim(a));
let $values1 = '  MySQL',default;
let $values2 = 'MySQL',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MAKE_SET()
let $cols = a varchar(10), b varchar(10), c int, d virtual varchar(30) as (make_set(c,a,b));
let $values1 = 'a','b',1,default;
let $values2 = 'a','b',3,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MID()
let $cols = a varchar(10), b virtual varchar(10) as (mid(a,1,2));
let $values1 = 'foobarbar',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # NOT LIKE
let $cols = a varchar(10), b virtual int as (a not like 'H%o');
let $values1 = 'Hello',default;
let $values2 = 'MySQL',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # OCTET_LENGTH()
let $cols = a varchar(10), b virtual int as (octet_length(a));
let $values1 = 'text',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # ORD()
let $cols = a varchar(10), b virtual int as (ord(a));
let $values1 = '2',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # POSITION()
let $cols = a varchar(10), b virtual varchar(10) as (position('bar' in a));
let $values1 = 'foobarbar',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # QUOTE()
let $cols = a varchar(10), b virtual varchar(10) as (quote(a));
let $values1 = 'Don\'t',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # REPEAT()
let $cols = a varchar(10), b virtual varchar(30) as (repeat(a,3));
let $values1 = 'MySQL',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # REPLACE()
let $cols = a varchar(10), b virtual varchar(30) as (replace(a,'aa','bb'));
let $values1 = 'maa',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # REVERSE()
let $cols = a varchar(10), b virtual varchar(30) as (reverse(a));
let $values1 = 'maa',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # RIGHT()
let $cols = a varchar(10), b virtual varchar(10) as (right(a,4));
let $values1 = 'foobarbar',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # RPAD()
let $cols = a varchar(10), b virtual varchar(10) as (rpad(a,4,'??'));
let $values1 = 'He',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # RTRIM();
let $cols = a varchar(10), b virtual varchar(10) as (rtrim(a));
let $values1 = 'Hello  ',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SPACE()
let $cols = a varchar(5), b virtual varchar(10) as (concat(a,space(5)));
let $values1 = 'Hello', default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # STRCMP()
let $cols = a varchar(9), b varchar(9), c virtual int as (strcmp(a,b));
let $values1 = 'Hello','Hello', default;
let $values2 = 'Hello','Hello1', default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SUBSTR()
let $cols = a varchar(5), b virtual varchar(10) as (substr(a,2));
let $values1 = 'Hello',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SUBSTRING_INDEX()
let $cols = a varchar(15), b virtual varchar(10) as (substring_index(a,'.',2));
let $values1 = 'www.mysql.com',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SUBSTRING()
let $cols = a varchar(5), b virtual varchar(10) as (substring(a from 2 for 2));
let $values1 = 'Hello',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TRIM()
let $cols = a varchar(15), b virtual varchar(10) as (trim(a));
let $values1 = ' aa ',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # UCASE()
let $cols = a varchar(5), b virtual varchar(10) as (ucase(a));
let $values1 = 'MySQL',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # UNHEX()
let $cols = a varchar(15), b virtual varchar(10) as (unhex(a));
let $values1 = '4D7953514C',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # UPPER()
let $cols = a varchar(5), b virtual varchar(10) as (upper(a));
let $values1 = 'MySQL',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo #
--echo # CONTROL FLOW FUNCTIONS
--echo #

--echo # CASE
let $cols = a varchar(10), b virtual varchar(16) as (case a when NULL then 'asd' when 'b' then 'B' else a end);
let $values1 = NULL,default;
let $values2 = 'b',default;
let $values3 = 'c',default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # IF
let $cols = a int, b int, c virtual int as (if(a=1,a,b));
let $values1 = 1,2,default;
let $values2 = 3,4,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # IFNULL
let $cols = a varchar(10), b varchar(10), c virtual varchar(10) as (ifnull(a,'DEFAULT'));
let $values1 = NULL,'adf',default;
let $values2 = 'a','adf',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # NULLIF
let $cols = a varchar(10), b virtual varchar(10) as (nullif(a,'DEFAULT'));
let $values1 = 'DEFAULT',default;
let $values2 = 'a',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo #
--echo # OPERATORS
--echo #

--echo # AND
let $cols = a int, b virtual int as (a>0 and a<2);
let $values1 = -1,default;
let $values2 = 1,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # BETWEEN ... AND ... 
let $cols = a int, b virtual int as (a between 0 and 2);
let $values1 = -1,default;
let $values2 = 1,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # BINARY
let $cols = a varchar(10), b virtual varbinary(10) as (binary a);
let $values1 = '11',default;
let $values2 = 1,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DIV
let $cols = a int, b virtual int as (a div 5);
let $values1 = 1,default;
let $values2 = 7,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # <=>
let $cols = a int, b int, c virtual int as (a <=> b);
let $values1 = 1,1,default;
let $values2 = NULL,NULL,default;
let $values3 = 1,NULL,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # =
let $cols = a varchar(10), b varchar(10), c virtual int as (a=b);
let $values1 = 'a','b',default;
let $values2 = 'a','a',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # >=
let $cols = a varchar(10), b varchar(10), c virtual int as (a >= b);
let $values1 = 'a','b',default;
let $values2 = 'a','a',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # >
let $cols = a varchar(10), b varchar(10), c virtual int as (a > b);
let $values1 = 'a','b',default;
let $values2 = 'a','a',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # IS NOT NULL
let $cols = a int, b virtual int as (a is not null);
let $values1 = 1,default;
let $values2 = NULL,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # IS NULL
let $cols = a int, b virtual int as (a is null);
let $values1 = 1,default;
let $values2 = NULL,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # <=
let $cols = a varchar(10), b varchar(10), c virtual int as (a <= b);
let $values1 = 'b','a',default;
let $values2 = 'b','b',default;
let $values3 = 'b','c',default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # <
let $cols = a varchar(10), b varchar(10), c virtual int as (a < b);
let $values1 = 'b','a',default;
let $values2 = 'b','b',default;
let $values3 = 'b','c',default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # NOT BETWEEN ... AND ...
let $cols = a int, b virtual int as (a not between 0 and 2);
let $values1 = -1,default;
let $values2 = 1,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # <>
let $cols = a varchar(10), b varchar(10), c virtual int as (a <> b);
let $values1 = 'b','a',default;
let $values2 = 'b','b',default;
let $values3 = 'b','c',default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # !=
let $cols = a varchar(10), b varchar(10), c virtual int as (a != b);
let $values1 = 'b','a',default;
let $values2 = 'b','b',default;
let $values3 = 'b','c',default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # OR
let $cols = a int, b virtual int as (a>5 or a<3);
let $values1 = 1,default;
let $values2 = 4,default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # XOR
let $cols = a int, b virtual int as (a xor 5);
let $values1 = 0,default;
let $values2 = 1,default;
let $values3 = 2,default;
let $rows = 3;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo #
--echo # DATE AND TIME FUNCTIONS
--echo #

--echo # ADDDATE()
let $cols = a datetime, b virtual datetime as (adddate(a,interval 1 month));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # ADDTIME()
let $cols = a datetime, b virtual datetime as (addtime(a,'02:00:00'));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DATE_ADD()
let $cols = a datetime, b virtual datetime as (date_add(a,interval 1 month));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DATE_FORMAT()
let $cols = a datetime, b virtual varchar(64) as (date_format(a,'%W %M %D'));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DATE_SUB()
let $cols = a datetime, b virtual datetime as (date_sub(a,interval 1 month));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DATE()
let $cols = a datetime, b virtual datetime as (date(a));
let $values1 = '2008-08-31 02:00:00',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DATEDIFF()
let $cols = a datetime, b virtual int as (datediff(a,'2000-01-01'));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DAY()
let $cols = a datetime, b virtual int as (day(a));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DAYNAME()
let $cols = a datetime, b virtual varchar(10) as (dayname(a));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DAYOFMONTH()
let $cols = a datetime, b virtual int as (dayofmonth(a));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DAYOFWEEK()
let $cols = a datetime, b virtual int as (dayofweek(a));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DAYOFYEAR()
let $cols = a datetime, b virtual int as (dayofyear(a));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # EXTRACT
let $cols = a datetime, b virtual int as (extract(year from a));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # FROM_DAYS()
let $cols = a int, b virtual datetime as (from_days(a));
let $values1 = 730669,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # FROM_UNIXTIME()
let $cols = a int, b virtual datetime as (from_unixtime(a));
let $values1 = 1196440219,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # GET_FORMAT()
let $cols = a datetime, b virtual varchar(32) as (date_format(a,get_format(DATE,'EUR')));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # HOUR()
let $cols = a time, b virtual int as (hour(a));
let $values1 = '10:05:03',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # LAST_DAY()
let $cols = a datetime, b virtual datetime as (last_day(a));
let $values1 = '2003-02-05',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MAKEDATE()
let $cols = a int, b virtual datetime as (makedate(a,1));
let $values1 = 2001,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MAKETIME()
let $cols = a int, b virtual time as (maketime(a,1,3));
let $values1 = 12,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MICROSECOND()
let $cols = a datetime, b virtual int as (microsecond(a));
let $values1 = '2009-12-31 12:00:00.123456',default;
let $values2 = '2009-12-31 23:59:59.000010',default;
let $rows = 2;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MINUTE()
let $cols = a datetime, b virtual int as (minute(a));
let $values1 = '2009-12-31 23:59:59.000010',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MONTH()
let $cols = a datetime, b virtual int as (month(a));
let $values1 = '2009-12-31 23:59:59.000010',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # MONTHNAME()
let $cols = a datetime, b virtual varchar(16) as (monthname(a));
let $values1 = '2009-12-31 23:59:59.000010',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # PERIOD_ADD()
let $cols = a int, b virtual int as (period_add(a,2));
let $values1 = 200801,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # PERIOD_DIFF()
let $cols = a int, b int, c virtual int as (period_diff(a,b));
let $values1 = 200802,200703,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # QUARTER()
let $cols = a datetime, b virtual int as (quarter(a));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SEC_TO_TIME()
let $cols = a int, b virtual time as (sec_to_time(a));
let $values1 = 2378,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SECOND()
let $cols = a datetime, b virtual int as (second(a));
let $values1 = '10:05:03',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # STR_TO_DATE()
let $cols = a varchar(64), b virtual datetime as (str_to_date(a,'%m/%d/%Y'));
let $values1 = '04/30/2004',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SUBDATE()
let $cols = a datetime, b virtual datetime as (subdate(a,interval 1 month));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # SUBTIME()
let $cols = a datetime, b virtual datetime as (subtime(a,'02:00:00'));
let $values1 = '2008-08-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TIME_FORMAT()
let $cols = a datetime, b virtual varchar(32) as (time_format(a,'%r'));
let $values1 = '2008-08-31 02:03:04',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TIME_TO_SEC()
let $cols = a time, b virtual int as (time_to_sec(a));
let $values1 = '22:23:00',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TIME()
let $cols = a datetime, b virtual time as (time(a));
let $values1 = '2008-08-31 02:03:04',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TIMEDIFF()
let $cols = a datetime, b datetime, c virtual int as (timediff(a,b));
let $values1 = '2008-12-31 23:59:59.000001','2008-12-30 01:01:01.000002',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TIMESTAMP()
let $cols = a datetime, b virtual timestamp as (timestamp(a));
let $values1 = '2008-12-31',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TIMESTAMPADD()
let $cols = a datetime, b virtual timestamp as (timestampadd(minute,1,a));
let $values1 = '2003-01-02',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TIMESTAMPDIFF()
let $cols = a timestamp, b timestamp, c virtual int as (timestampdiff(MONTH, a,b));
let $values1 = '2003-02-01','2003-05-01',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # TO_DAYS()
let $cols = a datetime, b virtual int as (to_days(a));
let $values1 = '2007-10-07',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # WEEK()
let $cols = a datetime, b virtual int as (week(a));
let $values1 = '2008-09-01',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # WEEKDAY()
let $cols = a datetime, b virtual int as (weekday(a));
let $values1 = '2008-09-01',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # WEEKOFYEAR()
let $cols = a datetime, b virtual int as (weekofyear(a));
let $values1 = '2008-09-01',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # YEAR()
let $cols = a datetime, b virtual int as (year(a));
let $values1 = '2008-09-01',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # YEARWEEK()
let $cols = a datetime, b virtual int as (yearweek(a));
let $values1 = '2008-09-01',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo #
--echo # FULL TEXT SEARCH FUNCTIONS
--echo #
--echo # None.

--echo #
--echo # CAST FUNCTIONS AND OPERATORS
--echo #

#--echo # CAST()
let $cols = a varchar(32), b virtual datetime as (cast(a as datetime));
let $values1 = '2008-10-09',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

#--echo # Convert()
let $cols = a varchar(32), b virtual datetime as (convert(a, datetime));
let $values1 = '2008-10-09',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo #
--echo # OTHER FUNCTIONS
--echo #

--echo # BIT_COUNT()
let $cols = a int, b virtual int as (bit_count(a));
let $values1 = 5,default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # COERCIBILITY()
let $cols = a varchar(1024), b virtual int as (coercibility(a));
let $values1 = 'abc',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # COLLATION()
let $cols = a varchar(1024), b virtual varchar(1024) as (collation(a));
let $values1 = 'abc',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc

--echo # DEFAULT()
let $cols = a varchar(1024) default 'aaa', b virtual varchar(1024) as (ifnull(a,default(a)));
let $values1 = 'any value',default;
let $rows = 1;
--source suite/vcol/inc/vcol_supported_sql_funcs.inc
