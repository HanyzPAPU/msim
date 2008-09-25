/*
 * Copyright (c) 2001-2007 Viliam Holub
 */

#ifdef HAVE_CONFIG_H
#	include "../config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "device.h"

#include "../mem.h"
#include "dcpu.h"
#include "dkeyboard.h"
#include "dorder.h"
#include "ddisk.h"
#include "dprinter.h"
#include "dtime.h"
#include "../check.h"
#include "../utils.h"



/* implemented pheriperal list */
const device_type_s *device_types[] =
{
	&DCPU,
	&DRWM,
	&DROM,
	&DPrinter,
	&DOrder,
	&DKeyboard,
	&DDisk,
	&DTime,
	0
};


/* List of all devices */
device_s *device_list = NULL;
/* List of all devices implement step command */
device_s *device_step_list = NULL;
/* List of all devices implement step command */
device_s *device_step4_list = NULL;

void
info_printf( const char *fmt, ...)
	
{
        va_list ap;
        va_start( ap, fmt);
	
	if (!(!fmt || !*fmt))
	{
		vfprintf( stdout, fmt, ap);
	}
	
	va_end( ap);
}


/** Iterates over the list of all devices.
 */
bool
dev_next( device_s **d)
{
	*d = *d ? (*d)->next : device_list;
	return !!*d;
}

/** Iterates over the list of all devices which implements step.
 */
bool
dev_next_in_step( device_s **d)
{
	*d = *d ? (*d)->next_in_step : device_step_list;
	return !!*d;
}

/** Iterates over the list of all devices which implements step4.
 */
bool
dev_next_in_step4( device_s **d)
{
	*d = *d ? (*d)->next_in_step4 : device_step4_list;
	return !!*d;
}


/** dev_by_partial_typename - Returns the first device type starting with
 * the specified name.
 */
const char *
dev_by_partial_typename( const char *name, const device_type_s ***dt)

{
	const device_type_s **d =
		(dt && *dt) ? *dt : &device_types[ -1];

	if (!name)
		name = "";

	while (*++d && !prefix( name, (*d)->name)) ;

	if (dt)
		*dt = d;

	return *d ? (*d)->name : NULL;
}


/** dev_by_partial_name - Returns the first device starting with the specified
 * name.
 *
 */
const char *
dev_by_partial_name( const char *name, device_s **d)

{
	PRE( d != NULL);

	if (!name)
		name = "";

	while (dev_next( d))
		if (prefix( name, (*d)->name))
			break;

	return *d ? (*d)->name : NULL;
}


/** devs_by_partial_name - Returns a number of devices specified by by the
 * specified prefix.
 *
 * The function returns a device structure of the last founded device.
 */
int
devs_by_partial_name( const char *name, device_s **d)

{
	int cnt = 0;
	device_s *dx = NULL;

	PRE( name != NULL, d != NULL);

	while (dev_next( &dx))
		if (prefix( name, dx->name))
		{
			cnt++;
			*d = dx;
		}

	return cnt;
}


/** Tests for name duplicity.
 *
 */
device_s *
dev_by_name( const char *s)

{
	device_s *d = NULL;

	while (dev_next( &d))
		if (!strcmp( s, d->name))
			break;

	return d;
}


bool
dev_map( void *data, bool (*f)(void *, device_s *))

{
	device_s *d;

	while (dev_next( &d))
		if (f( data, d))
			return true;

	return false;
}


void
cpr_num( char *s, uint32_t i)

{
	if (i == 0)
	{
		*s = '0';
		*(s+1) = 0;
	}
	else
	if ((i & 0xfffff) == 0)
		sprintf( s, "%dM", i >> 20);
	else
	if ((i & 0x3ff) == 0)
		sprintf( s, "%dk", i >> 10);
	else
	if ((i % 1000) == 0)
		sprintf( s, "%dK", i / 1000);
	else
		sprintf( s, "%d", i);
}


/** Add a new device into the list of devices.
 *
 * TODO
 * 	- rather add a device at the end of the list
 */
void
dev_add( device_s *d)
{
	/* Add to the list of all devices */
	d->next = device_list;
	device_list = d;

	/* If the step function is implemented, add the device to the list of
	 * step. */
	if (d->type->step) {
		d->next_in_step = device_step_list;
		device_step_list = d;
	}

	/* If the step4 function is implemented, add the device to the list of
	 * step4. */
	if (d->type->step4) {
		d->next_in_step4 = device_step4_list;
		device_step4_list = d;
	}
}


/** Removes a device from the machine.
 *
 * TODO
 * 	- check if it is correct
 * 	- remove from the list of steps as well
 */
void
dev_remove( device_s *d)

{
	device_s *g, *gx;

	if (d == device_list)
		device_list = d->next;
	else
	{
		for (g=device_list, gx=NULL; g && (g != d); g = g->next)
			gx = g;

		if (g)
			gx->next = g->next;
	}
}


/** Generic help generation.
 *
 * Function is designed to be used in device command specifications.
 * It is a general help generated automagically from the device command
 * specification.
 */
bool
dev_generic_help( parm_link_s *parm, device_s *dev)

{
	cmd_print_extended_help( parm, dev->type->cmds);
	
	return true;
}


/** Looks up to the command generator.
 */
/*
static void
find_dev_gen( parm_link_s **pl, const struct device_struct *d,
		gen_f *generator, void **data)

{
	PRE( pl != NULL, data != NULL, generator != NULL, d != NULL);
	PRE( *generator == NULL, *pl != NULL, *data == NULL);
	PRE( parm_type( *pl) != tt_end);

	if (parm_type( *pl) != tt_str)
	{
		// hmmm - theis no command specified
		// do not complete
		return;
	}
	
	// continue
	const cmd_struct cmds = ++d->cmds;

	while (

	if (dev_by_partial_typename( parm_str( *pl), &d))
	{
		if (dev_by_partial_typename( parm_str( *pl), &d))
		{
			if (parm_type( (*pl)->next) == tt_end)
			{
				*generator = generator_devtype; 
				return;
			}
			else
				// error
				return;
		}

		// fidn next -> device specific
		// test for full name
		if (parm_type( (*pl)->next) == tt_end)
		{
			*generator = generator_devtype; 
			return;
		}
		else
			// error
			return;
		
		*generator = generator_devtype; 
	}
}
*/


void
find_dev_gen( parm_link_s **pl, const device_s *d,
		gen_f *generator, const void **data)

{
	int res;
	const cmd_s *cmd;
	parm_link_s *plx;

	if (parm_type( *pl) != tt_str)
		// illegal command name
		return;
	
	// look up for device command
	switch (res = cmd_find( parm_str( *pl), d->type->cmds+1, &cmd))
	{
		case CMP_NH:
			// no such command
			return;
		case CMP_HIT:
		case CMP_MHIT:
			plx = *pl;
			parm_next( pl);
			if (parm_type( *pl) == tt_end)
			{
				// set the default command generator
				*generator = generator_cmd; 
				*data = d->type->cmds+1;
				*pl = plx;
				break;
			}
			else
			{
				if (res == CMP_MHIT)
					// input error
					break;

				// continue to the next generator, if possible
				if (cmd->find_gen)
					cmd->find_gen( pl, cmd, generator,
							data);
			}
			break;
	}
}

/** Tests whether the address is word-aligned.
 */
bool
addr_word_aligned( uint32_t addr)
{
	return (addr&0x3) == 0;
}