/*
 * rasqal_xsd_datatypes.c - Rasqal XML Schema Datatypes support
 *
 * Copyright (C) 2005-2010, David Beckett http://www.dajobe.org/
 * Copyright (C) 2005-2005, University of Bristol, UK http://www.bristol.ac.uk/
 * 
 * This package is Free Software and part of Redland http://librdf.org/
 * 
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 * 
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 * 
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 * 
 * 
 */

#ifdef HAVE_CONFIG_H
#include <rasqal_config.h>
#endif

#ifdef WIN32
#include <win32_rasqal_config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdarg.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "rasqal.h"
#include "rasqal_internal.h"


/*
 *
 * References
 *
 * XPath Functions and Operators
 * http://www.w3.org/TR/xpath-functions/
 *
 * Datatypes hierarchy
 * http://www.w3.org/TR/xpath-functions/#datatypes
 * 
 * Casting
 * http://www.w3.org/TR/xpath-functions/#casting-from-primitive-to-primitive
 *
 */


static int
rasqal_xsd_check_boolean_format(const unsigned char* string, int flags) 
{
  /* FIXME
   * Strictly only {true, false, 1, 0} are allowed according to
   * http://www.w3.org/TR/xmlschema-2/#boolean
   */
  if(!strcmp((const char*)string, "true") || 
     !strcmp((const char*)string, "TRUE") ||
     !strcmp((const char*)string, "1") ||
     !strcmp((const char*)string, "false") || 
     !strcmp((const char*)string, "FALSE") ||
     !strcmp((const char*)string, "0"))
    return 1;

  return 0;
}


#define ADVANCE_OR_DIE(p) if(!*(++p)) return 0;


/**
 * rasqal_xsd_check_dateTime_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD dateTime lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_dateTime_format(const unsigned char* string, int flags) 
{
  /* This should be correct according to 
   * http://www.w3.org/TR/xmlschema-2/#dateTime
   */
  return rasqal_xsd_datetime_check((const char*)string);
}


/**
 * rasqal_xsd_check_decimal_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD decimal lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_decimal_format(const unsigned char* string, int flags) 
{
  const char* p;
  
  /* This should be correct according to 
   * http://www.w3.org/TR/xmlschema-2/#decimal
   */
  p = (const char*)string;
  if(*p == '+' || *p == '-') {
    ADVANCE_OR_DIE(p);
  }

  while(*p && isdigit((int)*p))
    p++;
  if(!*p)
    return 1;
  /* Fail if first non-digit is not '.' */
  if(*p != '.')
    return 0;
  p++;
  
  while(*p && isdigit((int)*p))
    p++;
  /* Fail if anything other than a digit seen before NUL */
  if(*p)
    return 0;

  return 1;
}


/**
 * rasqal_xsd_check_double_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD double lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_double_format(const unsigned char* string, int flags) 
{
  /* FIXME validate using
   * http://www.w3.org/TR/xmlschema-2/#double
   */
  char* eptr = NULL;

  (void)strtod((const char*)string, &eptr);
  if((unsigned char*)eptr != string && *eptr == '\0')
    return 1;

  return 0;
}


/**
 * rasqal_xsd_check_float_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD float lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_float_format(const unsigned char* string, int flags) 
{
  /* FIXME validate using
   * http://www.w3.org/TR/xmlschema-2/#float
   */
  char* eptr = NULL;

  (void)strtod((const char*)string, &eptr);
  if((unsigned char*)eptr != string && *eptr == '\0')
    return 1;

  return 0;
}


/**
 * rasqal_xsd_check_integer_format:
 * @string: lexical form string
 * flags: flags
 *
 * INTERNAL - Check an XSD integer lexical form
 *
 * Return value: non-0 if the string is valid
 */
static int
rasqal_xsd_check_integer_format(const unsigned char* string, int flags)
{
  char* eptr = NULL;

  /* This should be correct according to 
   * http://www.w3.org/TR/xmlschema-2/#integer
   */

  errno = 0;
  (void)strtol((const char*)string, &eptr, 10);

  if((unsigned char*)eptr != string && *eptr == '\0' &&
     errno != ERANGE)
    return 1;

  return 0;
}


/**
 * rasqal_xsd_format_integer:
 * @i: integer
 * @len_p: pointer to length of result or NULL
 *
 * INTERNAL - Format a C integer as a string in XSD decimal integer format.
 *
 * This is suitable for multiple XSD decimal integer types that are
 * xsd:integer or sub-types such as xsd:short, xsd:int, xsd:long,
 *
 * See http://www.w3.org/TR/xmlschema-2/#built-in-datatypes for the full list.
 *
 * Return value: new string or NULL on failure
 */
unsigned char*
rasqal_xsd_format_integer(int i, size_t *len_p)
{
  unsigned char* string;
  
  /* Buffer sizes need to format:
   *   4:  8 bit decimal integers (xsd:byte)  "-128" to "127"
   *   6: 16 bit decimal integers (xsd:short) "-32768" to "32767" 
   *  11: 32 bit decimal integers (xsd:int)   "-2147483648" to "2147483647"
   *  20: 64 bit decimal integers (xsd:long)  "-9223372036854775808" to "9223372036854775807"
   * (the lexical form may have leading 0s in non-canonical representations)
   */
#define INTEGER_BUFFER_SIZE 20
  string = RASQAL_MALLOC(unsigned char*, INTEGER_BUFFER_SIZE + 1);
  if(!string)
    return NULL;
  /* snprintf() takes as length the buffer size including NUL */
  snprintf((char*)string, INTEGER_BUFFER_SIZE + 1, "%d", i);
  if(len_p)
    *len_p = strlen((const char*)string);

  return string;
}


/**
 * rasqal_xsd_format_float:
 * @i: float
 * @len_p: pointer to length of result or NULL
 *
 * INTERNAL - Format a new an xsd:float correctly
 *
 * Return value: new string or NULL on failure
 */
unsigned char*
rasqal_xsd_format_float(float f, size_t *len_p)
{
  unsigned char* string;
  
  /* FIXME: This is big enough for C float formatted in decimal as %1g */
#define FLOAT_BUFFER_SIZE 30
  string = RASQAL_MALLOC(unsigned char*, FLOAT_BUFFER_SIZE + 1);
  if(!string)
    return NULL;
  /* snprintf() takes as length the buffer size including NUL */
  /* FIXME: %1g may not be the nearest to XSD xsd:float canonical format */
  snprintf((char*)string, FLOAT_BUFFER_SIZE + 1, "%1g", (double)f);
  if(len_p)
    *len_p = strlen((const char*)string);

  return string;
}


/**
 * rasqal_xsd_format_double:
 * @d: double
 * @len_p: pointer to length of result or NULL
 *
 * INTERNAL - Format a new an xsd:double correctly
 *
 * Return value: new string or NULL on failure
 */
unsigned char*
rasqal_xsd_format_double(double d, size_t *len_p)
{
  unsigned int e_index = 0;
  int trailing_zero_start = -1;
  unsigned int exponent_start;
  size_t len = 0;
  unsigned char* buf = NULL;
  
  if(d == 0.0f) {
    len = 5;
    buf = RASQAL_MALLOC(unsigned char*, len + 1);
    if(!buf)
      return NULL;

    memcpy(buf, "0.0e0", len + 1);
    if(len_p)
      *len_p = len;
    return buf;
  }

  len = 20;
  buf = RASQAL_MALLOC(unsigned char*, len + 1);
  if(!buf)
    return NULL;
  
  /* snprintf needs the length + 1 because it writes a \0 too */
  snprintf((char*)buf, len + 1, "%1.14e", d);

  /* find the 'e' and start of mantissa trailing zeros */

  for( ; buf[e_index]; ++e_index) {
    if(e_index > 0 && buf[e_index] == '0' && buf[e_index-1] != '0')
      trailing_zero_start = (int)e_index;
    else if(buf[e_index] == 'e')
      break;
  }

  if(trailing_zero_start >= 0) {
    if(buf[trailing_zero_start-1] == '.')
      ++trailing_zero_start;

    /* write an 'E' where the trailing zeros started */
    buf[trailing_zero_start] = 'E';
    if(buf[e_index + 1] == '-') {
      buf[trailing_zero_start + 1] = '-';
      ++trailing_zero_start;
    }
  } else {
    buf[e_index] = 'E';
    trailing_zero_start = (int)e_index + 1;
  }
  
  exponent_start = e_index+2;
  while(buf[exponent_start] == '0')
    ++exponent_start;

  if(trailing_zero_start >= 0) {
    len = strlen((const char*)buf);
    if(exponent_start == len) {
      len = trailing_zero_start + 2;
      buf[len-1] = '0';
      buf[len] = '\0';
    } else {
      /* copy the exponent (minus leading zeros) after the new E */
      memmove(buf + trailing_zero_start + 1, buf + exponent_start,
              len - exponent_start);
      len = strlen((const char*)buf);
    }
  }
  
  if(len_p)
    *len_p = len;

  return buf;
}


typedef rasqal_literal* (*rasqal_extension_fn)(raptor_uri* name, raptor_sequence *args, char **error_p);


typedef struct {
  const unsigned char *name;
  int min_nargs;
  int max_nargs;
  rasqal_extension_fn fn;
  raptor_uri* uri;
} rasqal_xsd_datatype_fn_info;


#define XSD_INTEGER_DERIVED_COUNT 12
#define XSD_INTEGER_DERIVED_FIRST (RASQAL_LITERAL_LAST_XSD + 1)
#define XSD_INTEGER_DERIVED_LAST (RASQAL_LITERAL_LAST_XSD + XSD_INTEGER_DERIVED_COUNT-1)

/* atomic XSD literals + 12 types derived from xsd:integer plus a NULL */
#define SPARQL_XSD_NAMES_COUNT (RASQAL_LITERAL_LAST_XSD + 1 + XSD_INTEGER_DERIVED_COUNT)


static const char* const sparql_xsd_names[SPARQL_XSD_NAMES_COUNT + 1] =
{
  NULL, /* RASQAL_LITERAL_UNKNOWN */
  NULL, /* ...BLANK */
  NULL, /* ...URI */ 
  NULL, /* ...LITERAL */
  "string",
  "boolean",
  "integer", /* may type-promote all the way to xsd:decimal */
  "float",
  "double",
  "decimal",
  "dateTime",
  /* all of the following always type-promote to xsd:integer */
  "nonPositiveInteger", "negativeInteger",
  "long", "int", "short", "byte",
  "nonNegativeInteger", "unsignedLong", "postiveInteger",
  "unsignedInt", "unsignedShort", "unsignedByte",
  NULL
};


static int (*const sparql_xsd_checkfns[RASQAL_LITERAL_LAST_XSD-RASQAL_LITERAL_FIRST_XSD + 1])(const unsigned char* string, int flags) =
{
  NULL, /* RASQAL_LITERAL_STRING */
  rasqal_xsd_check_boolean_format, /* RASQAL_LITERAL_BOOLEAN */
  rasqal_xsd_check_integer_format, /* RASQAL_LITERAL_INTEGER */
  rasqal_xsd_check_double_format, /* RASQAL_LITERAL_DOUBLE */
  rasqal_xsd_check_float_format, /* RASQAL_LITERAL_FLOAT */
  rasqal_xsd_check_decimal_format, /* RASQAL_LITERAL_DECIMAL */
  rasqal_xsd_check_dateTime_format /* RASQAL_LITERAL_DATETIME */
};


int
rasqal_xsd_init(rasqal_world* world) 
{
  int i;

  world->xsd_namespace_uri = raptor_new_uri(world->raptor_world_ptr,
                                            raptor_xmlschema_datatypes_namespace_uri);
  if(!world->xsd_namespace_uri)
    return 1;

  world->xsd_datatype_uris = RASQAL_CALLOC(raptor_uri**, SPARQL_XSD_NAMES_COUNT + 1, sizeof(raptor_uri*));
  if(!world->xsd_datatype_uris)
    return 1;

  for(i = RASQAL_LITERAL_FIRST_XSD; i < SPARQL_XSD_NAMES_COUNT; i++) {
    const unsigned char* name = (const unsigned char*)sparql_xsd_names[i];
    world->xsd_datatype_uris[i] =
      raptor_new_uri_from_uri_local_name(world->raptor_world_ptr,
                                         world->xsd_namespace_uri, name);
    if(!world->xsd_datatype_uris[i])
      return 1;
  }

  return 0;
}


void
rasqal_xsd_finish(rasqal_world* world) 
{
  if(world->xsd_datatype_uris) {
    int i;
    
    for(i = RASQAL_LITERAL_FIRST_XSD; i < SPARQL_XSD_NAMES_COUNT; i++) {
      if(world->xsd_datatype_uris[i])
        raptor_free_uri(world->xsd_datatype_uris[i]);
    }

    RASQAL_FREE(table, world->xsd_datatype_uris);
    world->xsd_datatype_uris = NULL;
  }

  if(world->xsd_namespace_uri) {
    raptor_free_uri(world->xsd_namespace_uri);
    world->xsd_namespace_uri = NULL;
  }
}
 

  
rasqal_literal_type
rasqal_xsd_datatype_uri_to_type(rasqal_world* world, raptor_uri* uri)
{
  int i;
  rasqal_literal_type native_type = RASQAL_LITERAL_UNKNOWN;
  
  if(!uri || !world->xsd_datatype_uris)
    return native_type;
  
  for(i = (int)RASQAL_LITERAL_FIRST_XSD; i <= (int)XSD_INTEGER_DERIVED_LAST; i++) {
    if(raptor_uri_equals(uri, world->xsd_datatype_uris[i])) {
      if(i >= XSD_INTEGER_DERIVED_FIRST)
        native_type = RASQAL_LITERAL_INTEGER_SUBTYPE;
      else
        native_type = (rasqal_literal_type)i;
      break;
    }
  }
  return native_type;
}


raptor_uri*
rasqal_xsd_datatype_type_to_uri(rasqal_world* world, rasqal_literal_type type)
{
  if(world->xsd_datatype_uris &&
     type >= RASQAL_LITERAL_FIRST_XSD && type <= (int)RASQAL_LITERAL_LAST_XSD)
    return world->xsd_datatype_uris[(int)type];
  return NULL;
}


/**
 * rasqal_xsd_datatype_check:
 * @native_type: rasqal XSD type
 * @string: string
 * @flags: check flags
 *
 * INTERNAL - check a string as a valid lexical form of an XSD datatype
 *
 * Return value: non-0 if the string is valid
 */
int
rasqal_xsd_datatype_check(rasqal_literal_type native_type, 
                          const unsigned char* string, int flags)
{
  /* calculate check function index in sparql_xsd_checkfns table */
  int checkidx = native_type - RASQAL_LITERAL_FIRST_XSD;

  /* test for index out of bounds and check function not defined */
  if(checkidx < 0 || checkidx >= (int)(sizeof(sparql_xsd_checkfns)/sizeof(*sparql_xsd_checkfns)) ||
     !sparql_xsd_checkfns[checkidx])
    return 1;

  return sparql_xsd_checkfns[checkidx](string, flags);
}


const char*
rasqal_xsd_datatype_label(rasqal_literal_type native_type)
{
  return sparql_xsd_names[native_type];
}


int
rasqal_xsd_is_datatype_uri(rasqal_world* world, raptor_uri* uri)
{
  return (rasqal_xsd_datatype_uri_to_type(world, uri) != RASQAL_LITERAL_UNKNOWN);
}


int
rasqal_xsd_datatype_is_numeric(rasqal_literal_type type)
{
  return ((type >= RASQAL_LITERAL_BOOLEAN && type <= RASQAL_LITERAL_DECIMAL) ||
          (type == RASQAL_LITERAL_INTEGER_SUBTYPE));
}


static const rasqal_literal_type parent_xsd_type[RASQAL_LITERAL_LAST + 1] =
{
  /*   RASQAL_LITERAL_UNKNOWN  */  RASQAL_LITERAL_UNKNOWN,
  /* RDF Blank / RDF Term: Blank */
  /*   RASQAL_LITERAL_BLANK    */  RASQAL_LITERAL_UNKNOWN,
  /* RDF URI / RDF Term: URI */
  /*   RASQAL_LITERAL_URI      */  RASQAL_LITERAL_UNKNOWN,
  /* RDF Plain Literal / RDF Term: Literal */
  /*   RASQAL_LITERAL_STRING   */  RASQAL_LITERAL_UNKNOWN,
  /* XSD types / RDF Term Literal */
  /*   RASQAL_LITERAL_XSD_STRING */  RASQAL_LITERAL_UNKNOWN,
  /*   RASQAL_LITERAL_BOOLEAN  */  RASQAL_LITERAL_INTEGER,
  /*   RASQAL_LITERAL_INTEGER  */  RASQAL_LITERAL_FLOAT,
  /*   RASQAL_LITERAL_FLOAT    */  RASQAL_LITERAL_DOUBLE,
  /*   RASQAL_LITERAL_DOUBLE   */  RASQAL_LITERAL_DECIMAL,
  /*   RASQAL_LITERAL_DECIMAL  */  RASQAL_LITERAL_UNKNOWN,
  /*   RASQAL_LITERAL_DATETIME */  RASQAL_LITERAL_UNKNOWN,
  /* not datatypes */
  /*   RASQAL_LITERAL_UDT      */  RASQAL_LITERAL_UNKNOWN,
  /*   RASQAL_LITERAL_PATTERN  */  RASQAL_LITERAL_UNKNOWN,
  /*   RASQAL_LITERAL_QNAME    */  RASQAL_LITERAL_UNKNOWN,
  /*   RASQAL_LITERAL_VARIABLE */  RASQAL_LITERAL_UNKNOWN
};

rasqal_literal_type
rasqal_xsd_datatype_parent_type(rasqal_literal_type type)
{
  if(type == RASQAL_LITERAL_INTEGER_SUBTYPE)
    return RASQAL_LITERAL_INTEGER;
  
  if(type >= RASQAL_LITERAL_FIRST_XSD && type <= RASQAL_LITERAL_LAST_XSD)
    return parent_xsd_type[type];
  return RASQAL_LITERAL_UNKNOWN;
}
