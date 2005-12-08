/* env.h : managing the BDB environment
 *
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_FS_BDB_ENV_H
#define SVN_LIBSVN_FS_BDB_ENV_H

#define APU_WANT_DB
#include <apu_want.h>

#include <apr_pools.h>

#include "bdb_compat.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define BDB_ERRCALL_BATON_ERRPFX_STRING "svn (bdb): "

/* The Berkeley DB environment descriptor. */
typedef struct
{
  /* Berkeley DB returns extended error info by callback before returning
     an error code from the failing function.  The callback baton type is a
     string, not an arbitrary struct, so we prefix our struct with a valid
     string, to avoid problems should BDB ever try to interpret our baton as
     a string.  Initializers of this structure must strcpy the value of
     BDB_ERRCALL_BATON_ERRPFX_STRING into this array.  */
  char errpfx_string[sizeof(BDB_ERRCALL_BATON_ERRPFX_STRING)];

  /* The Berkeley DB environment. */
  DB_ENV *env;

  /* We hold the extended info here until the Berkeley DB function returns.
     It usually returns an error code, triggering the collection and
     wrapping of the additional errors stored here.

     Note: In some circumstances BDB will call the error function and not
     go on to return an error code, so the caller must always check whether
     pending_errors is non-NULL to avoid leaking errors.  This behaviour
     has been seen when running recovery on a repository upgraded to 4.3
     that still has old 4.2 log files present, a typical error string is
     "Skipping log file db/log.0000000002: historic log version 8" */
  svn_error_t *pending_errors;

  /* We permitted clients of our library to install a Berkeley BDB errcall.
     Since we now use the errcall ourselves, we must store and invoke a user
     errcall, to maintain our API guarantees. */
  void (*user_callback) (const char *errpfx, char *msg);
} bdb_env_t;



/* Flag combination for opening a shared BDB environment. */
#define SVN_BDB_STANDARD_ENV_FLAGS (DB_CREATE       \
                                    | DB_INIT_LOCK  \
                                    | DB_INIT_LOG   \
                                    | DB_INIT_MPOOL \
                                    | DB_INIT_TXN   \
                                    | SVN_BDB_AUTO_RECOVER)

/* Flag combination for opening a private BDB environment. */
#define SVN_BDB_PRIVATE_ENV_FLAGS (DB_CREATE       \
                                   | DB_INIT_LOG   \
                                   | DB_INIT_MPOOL \
                                   | DB_INIT_TXN   \
                                   | DB_PRIVATE)


/* Allocate the Berkeley DB descriptor BDB and open the environment.
 *
 * Open (*BDBP)->env in PATH, using FLAGS and MODE.  If applicable, set
 * the BDB_AUTO_COMMIT flag for this environment.
 *
 * Use POOL for temporary allocation.
 *
 * Note: This function may return a pointer to an existing
 * @c bdb_env_t object with a previously opened environment.
 */
svn_error_t *svn_fs_bdb__open (bdb_env_t **bdbp, const char *path,
                               u_int32_t flags, int mode,
                               apr_pool_t *pool);

/* Close the Berkeley DB descriptor BDB.
 *
 * Note: This function might not actually close the environment if it
 * has been "svn_fs_bdb__open'd" more than once.
 */
svn_error_t *svn_fs_bdb__close (bdb_env_t *bdb);


/* Remove the Berkeley DB environment at PATH.
 *
 * Use POOL for temporary allocation.
 *
 * This function will fail if the environment is already open.
 */
svn_error_t *svn_fs_bdb__remove (const char *path, apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_BDB_ENV_H */
