#ifndef MYSQL_SERVICE_ENCRYPTION_ROTATION_INCLUDED
/* Copyright (c) 2021, MariaDB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

/**
  @file
  encryption rotation service
*/
#ifdef __cplusplus
extern "C" {
#endif

#ifndef MYSQL_ABI_CHECK
#include <stdlib.h>
#endif

extern struct encryption_rotation_service_st {
 int (*encrypt_set_no_rotate_ptr)(void);
 int (*encrypt_get_no_rotate_ptr)(void);
} *encryption_rotation_service;

#ifdef MYSQL_DYNAMIC_PLUGIN

#define encryption_set_no_rotation() encryption_rotation_service->encrypt_set_no_rotate_ptr()
#define encryption_get_no_rotation() encryption_rotation_service->encrypt_get_no_rotate_ptr()
#else

/* Set encryption rotation variable */
int encryption_set_no_rotation(void);

int encryption_get_no_rotation(void);

#endif

#ifdef __cplusplus
}
#endif

#define MYSQL_SERVICE_ENCRYPTION_ROTATION_INCLUDED
#endif
