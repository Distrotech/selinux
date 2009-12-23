/* Authors:	Caleb Case	<ccase@tresys.com>
 *
 * Copyright (C) 2009 Tresys Technology, LLC
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _SEMANAGE_PRIVATE_HANDLE_H_
#define _SEMANAGE_PRIVATE_HANDLE_H_

#include <stdint.h>

/* Get the default priority. */
uint16_t semanage_get_default_priority(semanage_handle_t *sh);

/* Set the default priority. */
int semanage_set_default_priority(semanage_handle_t *sh, uint16_t priority);

#endif
