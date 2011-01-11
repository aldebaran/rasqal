/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rasqal_result_formats.c - Rasqal Query Result Format Class
 *
 * Copyright (C) 2003-2010, David Beckett http://www.dajobe.org/
 * Copyright (C) 2003-2005, University of Bristol, UK http://www.bristol.ac.uk/
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

#include "rasqal.h"
#include "rasqal_internal.h"



/*
 * rasqal_world_register_query_result_format_factory:
 * @world: rasqal world
 * @register_factory: pointer to function to call to register the factory
 * 
 * INTERNAL - Register a query result format via a factory.
 *
 * All strings set in the @factory method are shared with the
 * #rasqal_query_result_format_factory
 *
 * Return value: new factory object or NULL on failure
 **/
rasqal_query_results_format_factory*
rasqal_world_register_query_results_format_factory(rasqal_world* world,
                                                   int (*register_factory)(rasqal_query_results_format_factory*))
{
  rasqal_query_results_format_factory *factory = NULL;
  
  factory = (rasqal_query_results_format_factory*)RASQAL_CALLOC(rasqal_query_factory_factory, 1,
                                                                sizeof(*factory));
  if(!factory)
    return NULL;

  factory->world = world;

  factory->desc.mime_types = NULL;
  
  if(raptor_sequence_push(world->query_results_formats, factory))
    return NULL; /* on error, factory is already freed by the sequence */
  
  /* Call the factory registration function on the new object */
  if(register_factory(factory))
    /* factory is owned and freed by the query_results_formats sequence */
    return NULL;
  
  if(!factory->desc.names || !factory->desc.names[0] ||
     !factory->desc.label) {
    rasqal_log_error_simple(world, RAPTOR_LOG_LEVEL_ERROR, NULL,
                            "Result query result format failed to register required names and label fields\n");
    goto tidy;
  }

  factory->desc.flags = 0;
  if(factory->get_rowsource)
    factory->desc.flags |= RASQAL_QUERY_RESULTS_FORMAT_FLAG_READER;
  if(factory->write)
    factory->desc.flags |= RASQAL_QUERY_RESULTS_FORMAT_FLAG_WRITER;


#ifdef RASQAL_DEBUG
  /* Maintainer only check of static data */
  if(factory->desc.mime_types) {
    unsigned int i;
    const raptor_type_q* type_q = NULL;

    for(i = 0; 
        (type_q = &factory->desc.mime_types[i]) && type_q->mime_type;
        i++) {
      size_t len = strlen(type_q->mime_type);
      if(len != type_q->mime_type_len) {
        fprintf(stderr,
                "Query result format %s  mime type %s  actual len %d  static len %d\n",
                factory->desc.names[0], type_q->mime_type,
                (int)len, (int)type_q->mime_type_len);
      }
    }

    if(i != factory->desc.mime_types_count) {
        fprintf(stderr,
                "Query result format %s  saw %d mime types  static count %d\n",
                factory->desc.names[0], i, factory->desc.mime_types_count);
    }
  }
#endif

#if defined(RASQAL_DEBUG) && RASQAL_DEBUG > 1
  RASQAL_DEBUG3("Registered query result format %s with context size %d\n",
                factory->names[0], factory->context_length);
#endif

  return factory;

  /* Clean up on failure */
  tidy:
  rasqal_free_query_results_format_factory(factory);
  return NULL;
}


void
rasqal_free_query_results_format_factory(rasqal_query_results_format_factory* factory)
{
  RASQAL_FREE(query_results_format_factory, factory);
}


int
rasqal_init_result_formats(rasqal_world* world)
{
  int rc = 0;
  
  world->query_results_formats = raptor_new_sequence((raptor_data_free_handler)rasqal_free_query_results_format_factory, NULL);
  if(!world->query_results_formats)
    return 1;

  rc += rasqal_init_result_format_sparql_xml(world) != 0;

  rc += rasqal_init_result_format_json(world) != 0;

  rc += rasqal_init_result_format_table(world) != 0;

  rc += rasqal_init_result_format_sv(world) != 0;

  rc += rasqal_init_result_format_html(world) != 0;

  rc += rasqal_init_result_format_turtle(world) != 0;

  rc += rasqal_init_result_format_rdf(world) != 0;

  return rc;
}


void
rasqal_finish_result_formats(rasqal_world* world)
{
  if(world->query_results_formats) {
    raptor_free_sequence(world->query_results_formats);
    world->query_results_formats = NULL;
  }
}


static rasqal_query_results_format_factory*
rasqal_get_query_results_formatter_factory(rasqal_world* world,
                                           const char *name, raptor_uri* uri,
                                           const char *mime_type,
                                           int flags)
{
  int i;
  rasqal_query_results_format_factory* factory = NULL;
  
  for(i = 0; 1; i++) {
    int factory_flags = 0;
    
    factory = (rasqal_query_results_format_factory*)raptor_sequence_get_at(world->query_results_formats,
                                                                           i);
    if(!factory)
      break;

    if(factory->get_rowsource)
      factory_flags |= RASQAL_QUERY_RESULTS_FORMAT_FLAG_READER;
    if(factory->write)
      factory_flags |= RASQAL_QUERY_RESULTS_FORMAT_FLAG_WRITER;

    /* Flags must match */
    if(flags && factory_flags != flags)
      continue;

    if(!name && !uri)
      /* the default is the first registered format */
      break;

    if(name) {
      int namei;
      const char* fname;
      
      for(namei = 0; (fname = factory->desc.names[namei]); namei++) {
        if(!strcmp(fname, name))
          return factory;
      }
    }

    if(uri && factory->desc.uri_strings) {
      int j;
      const char* uri_string = (const char*)raptor_uri_as_string(uri);
      const char* factory_uri_string = NULL;
      
      for(j = 0;
          (factory_uri_string = factory->desc.uri_strings[j]);
          j++) {
        if(!strcmp(uri_string, factory_uri_string))
          break;
      }
      if(factory_uri_string)
        /* got an exact match syntax for URI - return result */
        break;
    }

    if(mime_type) {
      int mti;
      const char* mname;
      
      for(mti = 0; (mname = factory->desc.mime_types[mti].mime_type); mti++) {
        if(!strcmp(mname, mime_type))
          return factory;
      }
    }
  }
  
  return factory;
}


/**
 * rasqal_query_results_formats_check:
 * @world: rasqal_world object
 * @name: the query results format name (or NULL)
 * @uri: #raptor_uri query results format uri (or NULL)
 * @mime_type: mime type name
 * @flags: bitmask of flags to signify that format is needed for reading (#RASQAL_QUERY_RESULTS_FORMAT_FLAG_READER ) or writing ( #RASQAL_QUERY_RESULTS_FORMAT_FLAG_WRITER )
 * 
 * Check if a query results formatter exists for the requested format.
 * 
 * Return value: non-0 if a formatter exists.
 **/
int
rasqal_query_results_formats_check(rasqal_world* world,
                                   const char *name, raptor_uri* uri,
                                   const char *mime_type,
                                   int flags)
{
  rasqal_query_results_format_factory* factory = NULL;
  
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(world, rasqal_world, 1);

  factory = rasqal_get_query_results_formatter_factory(world,
                                                       name, uri, mime_type,
                                                       flags);
  return (factory != NULL);
}


/**
 * rasqal_new_query_results_formatter:
 * @world: rasqal_world object
 * @name: the query results format name (or NULL)
 * @mime_type: the query results format mime type (or NULL)
 * @format_uri: #raptor_uri query results format uri (or NULL)
 *
 * Constructor - create a new rasqal_query_results_formatter for an identified format.
 *
 * A query results format can be found by name, mime type or URI, all
 * of which are optional.  If multiple fields are given, the first
 * match is given that matches the name, URI, mime_type in that
 * order.  The default query results format will be used if all are
 * format identifying fields are NULL.
 *
 * See rasqal_world_get_query_results_format_description() for
 * obtaining the supported format URIs at run time.
 *
 * Return value: a new #rasqal_query_results_formatter object or NULL on failure
 */
rasqal_query_results_formatter*
rasqal_new_query_results_formatter(rasqal_world* world,
                                   const char *name, 
                                   const char *mime_type,
                                   raptor_uri* format_uri)
{
  rasqal_query_results_format_factory* factory;
  rasqal_query_results_formatter* formatter;
  int flags = 0;

  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(world, rasqal_world, NULL);

  factory = rasqal_get_query_results_formatter_factory(world, name, 
                                                       format_uri, mime_type,
                                                       flags);
  if(!factory)
    return NULL;

  formatter = (rasqal_query_results_formatter*)RASQAL_CALLOC(rasqal_query_results_formatter, 1, sizeof(*formatter));
  if(!formatter)
    return NULL;

  formatter->factory = factory;

  formatter->context = NULL;
  if(factory->context_length) {
    formatter->context = (char*)RASQAL_CALLOC(rasqal_query_results_formatter_context, 1,
                                              factory->context_length);
    if(!formatter->context) {
      rasqal_free_query_results_formatter(formatter);
    return NULL;
    }
  }
  
  if(formatter->factory->init) {
    if(formatter->factory->init(formatter, name)) {
      rasqal_free_query_results_formatter(formatter);
      return NULL;
    }
  }
  
  return formatter;
}


/**
 * rasqal_new_query_results_formatter_for_content:
 * @world: world object
 * @uri: URI identifying the syntax (or NULL)
 * @mime_type: mime type identifying the content (or NULL)
 * @buffer: buffer of content to guess (or NULL)
 * @len: length of buffer
 * @identifier: identifier of content (or NULL)
 * 
 * Constructor - create a new queryresults formatter for an identified format.
 *
 * Uses rasqal_world_guess_query_results_format_name() to find a
 * query results formatter by scoring recognition of the syntax by a
 * block of characters, the content identifier or a mime type.  The
 * content identifier is typically a filename or URI or some other
 * identifier.
 * 
 * Return value: a new #rasqal_query_results_formatter object or NULL on failure
 **/
rasqal_query_results_formatter*
rasqal_new_query_results_formatter_for_content(rasqal_world* world,
                                               raptor_uri *uri,
                                               const char *mime_type,
                                               const unsigned char *buffer,
                                               size_t len,
                                               const unsigned char *identifier)
{
  const char* name;

  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(world, rasqal_world, NULL);

  name = rasqal_world_guess_query_results_format_name(world,
                                                      uri, mime_type,
                                                      buffer, len,
                                                      identifier);
  return name ? rasqal_new_query_results_formatter(world, name, NULL, NULL) : NULL;
}


/**
 * rasqal_free_query_results_formatter:
 * @formatter: #rasqal_query_results_formatter object
 * 
 * Destructor - destroy a #rasqal_query_results_formatter object.
 **/
void
rasqal_free_query_results_formatter(rasqal_query_results_formatter* formatter) 
{
  if(!formatter)
    return;

  if(formatter->factory->finish)
    formatter->factory->finish(formatter);
  
  if(formatter->context)
    RASQAL_FREE(context, formatter->context);

  RASQAL_FREE(rasqal_query_results_formatter, formatter);
}


/**
 * rasqal_world_get_query_results_format_description:
 * @world: world object
 * @counter: index into the list of query result formats
 *
 * Get query result format descriptive syntax information
 *
 * Return value: description or NULL if counter is out of range
 **/
const raptor_syntax_description*
rasqal_world_get_query_results_format_description(rasqal_world* world, 
                                                  unsigned int counter)
{
  rasqal_query_results_format_factory* factory;

  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(world, rasqal_world, NULL);

  rasqal_world_open(world);
  
  factory = (rasqal_query_results_format_factory*)raptor_sequence_get_at(world->query_results_formats,
                                                                         counter);

  if(!factory)
    return NULL;

  return &factory->desc;
}


/**
 * rasqal_query_results_formatter_write:
 * @iostr: #raptor_iostream to write the query to
 * @formatter: #rasqal_query_results_formatter object
 * @results: #rasqal_query_results query results format
 * @base_uri: #raptor_uri base URI of the output format (or NULL)
 *
 * Write the query results using the given formatter to an iostream
 *
 * Note that after calling this method, the query results will be
 * empty and rasqal_query_results_finished() will return true (non-0)
 * 
 * See rasqal_world_get_query_results_format_description() for
 * obtaining the supported format URIs at run time.
 *
 * Return value: non-0 on failure
 **/
int
rasqal_query_results_formatter_write(raptor_iostream *iostr,
                                     rasqal_query_results_formatter* formatter,
                                     rasqal_query_results* results,
                                     raptor_uri *base_uri)
{
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(iostr, raptor_iostream, 1);
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(formatter, rasqal_query_results_formatter, 1);
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(results, rasqal_query_results, 1);

  if(!formatter->factory->write)
     return 1;
  return formatter->factory->write(formatter, iostr, results, base_uri);
}


/**
 * rasqal_query_results_formatter_read:
 * @world: rasqal world object
 * @iostr: #raptor_iostream to read the query from
 * @formatter: #rasqal_query_results_formatter object
 * @results: #rasqal_query_results query results format
 * @base_uri: #raptor_uri base URI of the input format
 *
 * Read the query results using the given formatter from an iostream
 * 
 * See rasqal_world_get_query_results_format_description() for
 * obtaining the supported format URIs at run time.
 *
 * Return value: non-0 on failure
 **/
int
rasqal_query_results_formatter_read(rasqal_world *world,
                                    raptor_iostream *iostr,
                                    rasqal_query_results_formatter* formatter,
                                    rasqal_query_results* results,
                                    raptor_uri *base_uri)
{
  rasqal_rowsource* rowsource = NULL;
  
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(world, rasqal_world, 1);
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(iostr, raptor_iostream, 1);
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(formatter, rasqal_query_results_formatter, 1);
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(results, rasqal_query_results, 1);
  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(base_uri, raptor_uri, 1);

  if(!formatter->factory->get_rowsource)
    return 1;
  
  rowsource = formatter->factory->get_rowsource(formatter, world, 
                                                rasqal_query_results_get_variables_table(results),
                                                iostr, base_uri);
  if(!rowsource)
    return 1;

  while(1) {
    rasqal_row* row = rasqal_rowsource_read_row(rowsource);
    if(!row)
      break;
    rasqal_query_results_add_row(results, row);
  }

  if(rowsource)
    rasqal_free_rowsource(rowsource);
  
  return 0;
}


struct syntax_score
{
  int score;
  rasqal_query_results_format_factory *factory;
};


static int
compare_syntax_score(const void *a, const void *b) {
  return ((struct syntax_score*)b)->score - ((struct syntax_score*)a)->score;
}
  

/**
 * rasqal_world_guess_query_results_format_name:
 * @world: world object
 * @uri: URI identifying the syntax (or NULL)
 * @mime_type: mime type identifying the content (or NULL)
 * @buffer: buffer of content to guess (or NULL)
 * @len: length of buffer
 * @identifier: identifier of content (or NULL)
 *
 * Guess a query results format name for content.
 * 
 * Find a query results format by scoring recognition of the syntax
 * by a block of characters, the content identifier or a mime type.
 * The content identifier is typically a filename or URI or some
 * other identifier.
 * 
 * Return value: a query results format name or NULL if no guess
 * could be made
 **/
const char*
rasqal_world_guess_query_results_format_name(rasqal_world* world,
                                             raptor_uri *uri,
                                             const char *mime_type,
                                             const unsigned char *buffer,
                                             size_t len,
                                             const unsigned char *identifier)
{
  unsigned int i;
  rasqal_query_results_format_factory *factory;
  unsigned char *suffix = NULL;
  struct syntax_score* scores;

  RASQAL_ASSERT_OBJECT_POINTER_RETURN_VALUE(world, rasqal_world, NULL);

  scores = (struct syntax_score*)RASQAL_CALLOC(syntax_scores,
                                               raptor_sequence_size(world->query_results_formats),
                                               sizeof(struct syntax_score));
  if(!scores)
    return NULL;
  
  if(identifier) {
    unsigned char *p = (unsigned char*)strrchr((const char*)identifier, '.');
    if(p) {
      unsigned char *from, *to;

      p++;
      suffix = (unsigned char*)RASQAL_MALLOC(cstring,
                                             strlen((const char*)p) + 1);
      if(!suffix)
        return NULL;

      for(from = p, to = suffix; *from; ) {
        unsigned char c = *from++;
        /* discard the suffix if it wasn't '\.[a-zA-Z0-9]+$' */
        if(!isalpha(c) && !isdigit(c)) {
          RASQAL_FREE(cstring, suffix);
          suffix = NULL;
          to = NULL;
          break;
        }
        *to++ = isupper(c) ? (unsigned char)tolower(c): c;
      }
      if(to)
        *to = '\0';
    }
  }

  for(i = 0;
      (factory = (rasqal_query_results_format_factory*)raptor_sequence_get_at(world->query_results_formats, i));
      i++) {
    int score = -1;
    const raptor_type_q* type_q = NULL;
    
    if(mime_type && factory->desc.mime_types) {
      int j;
      type_q = NULL;
      for(j = 0; 
          (type_q = &factory->desc.mime_types[j]) && type_q->mime_type;
          j++) {
        if(!strcmp(mime_type, type_q->mime_type))
          break;
      }
      /* got an exact match mime type - score it via the Q */
      if(type_q)
        score = type_q->q;
    }
    /* mime type match has high Q - return result */
    if(score >= 10)
      break;
    
    if(uri && factory->desc.uri_strings) {
      int j;
      const char* uri_string = (const char*)raptor_uri_as_string(uri);
      const char* factory_uri_string = NULL;
      
      for(j = 0;
          (factory_uri_string = factory->desc.uri_strings[j]);
          j++) {
        if(!strcmp(uri_string, factory_uri_string))
          break;
      }
      if(factory_uri_string)
        /* got an exact match syntax for URI - return result */
        break;
    }
    
    if(factory->recognise_syntax) {
      int c = -1;
    
      /* Only use first N bytes to avoid HTML documents that contain examples */
#define FIRSTN 1024
      if(buffer && len && len > FIRSTN) {
        c = buffer[FIRSTN];
        ((char*)buffer)[FIRSTN] = '\0';
      }

      score += factory->recognise_syntax(factory, buffer, len, 
                                         identifier, suffix, 
                                         mime_type);

      if(c >= 0)
        ((char*)buffer)[FIRSTN] = c;
    }

    scores[i].score = score < 10 ? score : 10; 
    scores[i].factory = factory;
#if RASQAL_DEBUG > 2
    RASQAL_DEBUG3("Score %15s : %d\n", factory->name, score);
#endif
  }
  
  if(!factory) {
    /* sort the scores and pick a factory */
    qsort(scores, i, sizeof(struct syntax_score), compare_syntax_score);
    if(scores[0].score >= 0)
      factory = scores[0].factory;
  }

  if(suffix)
    RASQAL_FREE(cstring, suffix);

  RASQAL_FREE(syntax_scores, scores);
  
  return factory ? factory->desc.names[0] : NULL;
}
