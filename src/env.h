/*
 * Environment variables
 * 
 * Copyright (c) 2002-2004 Viliam Holub 
 */


#ifndef _ENV_H_
#define _ENV_H_

#include "mtypes.h"
#include "parser.h"

/*
 * System variables
 */
extern bool iaddr;
extern bool iopc;
extern bool icmt;
extern bool iregch;
extern int ireg;

extern bool totrace;


extern char **regname;


/*
 * Variable types
 */
enum var_type_e
{
	vt_int,
	vt_str,
	vt_bool
};
typedef enum var_type_e var_type_e;


/*
 * System command implementation
 */
bool env_cmd_set( parm_link_s *pl);
bool env_cmd_unset( parm_link_s *pl);


/*
 * Routines
 */
int  env_cnt_partial_varname( const char *name);
bool env_check_varname( const char *name, var_type_e *type);
bool env_bool_type( const char *name);

/*
 * TAB completion
 */
char *generator_env_name( parm_link_s *pl, const void *data, int level);
char *generator_env_booltype( parm_link_s *pl, const void *data, int level);
char *generator_bool_envname( parm_link_s *pl, const void *data, int level);
char *generator_equal_char( parm_link_s *pl, const void *data, int level);

#endif /* _ENV_H_ */