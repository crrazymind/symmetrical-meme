#include "svn_delta.h"
#include "svn_time.h"
#include "svn_sorts.h"

/*
 * Constant separator strings
 */
static const char equal_string[] =
  "===================================================================";
static const char under_string[] =
  "___________________________________________________________________";


/*-----------------------------------------------------------------*/
/* Wrapper for apr_file_printf(), which see.  FORMAT is a utf8-encoded
   string after it is formatted, so this function can convert it to
   ENCODING before printing. */
static svn_error_t *
file_printf_from_utf8(apr_file_t *fptr, const char *encoding,
                      const char *format, ...)
  __attribute__ ((format(printf, 3, 4)));
static svn_error_t *
file_printf_from_utf8(apr_file_t *fptr, const char *encoding,
                      const char *format, ...)
{
  va_list ap;
  const char *buf, *buf_apr;

  va_start(ap, format);
  buf = apr_pvsprintf(apr_file_pool_get(fptr), format, ap);
  va_end(ap);

  SVN_ERR(svn_utf_cstring_from_utf8_ex2(&buf_apr, buf, encoding,
                                        apr_file_pool_get(fptr)));

  return svn_io_file_write_full(fptr, buf_apr, strlen(buf_apr),
                                NULL, apr_file_pool_get(fptr));
}


/* A helper function for display_prop_diffs.  Output the differences between
   the mergeinfo stored in ORIG_MERGEINFO_VAL and NEW_MERGEINFO_VAL in a
   human-readable form to FILE, using ENCODING.  Use POOL for temporary
   allocations. */
static svn_error_t *
display_mergeinfo_diff(const char *old_mergeinfo_val,
                       const char *new_mergeinfo_val,
                       const char *encoding,
                       apr_file_t *file,
                       apr_pool_t *pool)
{
  apr_hash_t *old_mergeinfo_hash, *new_mergeinfo_hash, *added, *deleted;
  apr_hash_index_t *hi;

  if (old_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&old_mergeinfo_hash, old_mergeinfo_val, pool));
  else
    old_mergeinfo_hash = NULL;

  if (new_mergeinfo_val)
    SVN_ERR(svn_mergeinfo_parse(&new_mergeinfo_hash, new_mergeinfo_val, pool));
  else
    new_mergeinfo_hash = NULL;

  SVN_ERR(svn_mergeinfo_diff(&deleted, &added, old_mergeinfo_hash,
                             new_mergeinfo_hash,
                             TRUE, pool));

  for (hi = apr_hash_first(pool, deleted);
       hi; hi = apr_hash_next(hi))
    {
      const char *from_path = svn__apr_hash_index_key(hi);
      apr_array_header_t *merge_revarray = svn__apr_hash_index_val(hi);
      svn_string_t *merge_revstr;

      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray, pool));

      SVN_ERR(file_printf_from_utf8(file, encoding,
                                    _("   Reverse-merged %s:r%s%s"),
                                    from_path, merge_revstr->data,
                                    APR_EOL_STR));
    }

  for (hi = apr_hash_first(pool, added);
       hi; hi = apr_hash_next(hi))
    {
      const char *from_path = svn__apr_hash_index_key(hi);
      apr_array_header_t *merge_revarray = svn__apr_hash_index_val(hi);
      svn_string_t *merge_revstr;

      SVN_ERR(svn_rangelist_to_string(&merge_revstr, merge_revarray, pool));

      SVN_ERR(file_printf_from_utf8(file, encoding,
                                    _("   Merged %s:r%s%s"),
                                    from_path, merge_revstr->data,
                                    APR_EOL_STR));
    }

  return SVN_NO_ERROR;
}
/* A helper function used by display_prop_diffs.
   TOKEN is a string holding a property value.
   If TOKEN is empty, or is already terminated by an EOL marker,
   return TOKEN unmodified. Else, return a new string consisting
   of the concatenation of TOKEN and the system's default EOL marker.
   The new string is allocated from POOL.
   If HAD_EOL is not NULL, indicate in *HAD_EOL if the token had a EOL. */
static const svn_string_t *
maybe_append_eol(const svn_string_t *token, svn_boolean_t *had_eol,
                 apr_pool_t *pool)
{
  const char *curp;

  if (had_eol)
    *had_eol = FALSE;

  if (token->len == 0)
    return token;

  curp = token->data + token->len - 1;
  if (*curp == '\r')
    {
      if (had_eol)
        *had_eol = TRUE;
      return token;
    }
  else if (*curp != '\n')
    {
      return svn_string_createf(pool, "%s%s", token->data, APR_EOL_STR);
    }
  else
    {
      if (had_eol)
        *had_eol = TRUE;
      return token;
    }
}

/* Adjust PATH to be relative to the repository root beneath ORIG_TARGET,
 * using RA_SESSION and WC_CTX, and return the result in *ADJUSTED_PATH.
 * ORIG_TARGET is one of the original targets passed to the diff command,
 * WC_ROOT_ABSPATH is the absolute path to the root directory of a working
 * copy involved in a repos-wc diff, and may be NULL.
adjust_relative_to_repos_root(const char **adjusted_path,
                              const char *path,
                              const char *orig_target,
                              svn_ra_session_t *ra_session,
                              svn_wc_context_t *wc_ctx,
                              const char *wc_root_abspath,
                              apr_pool_t *pool)
  const char *orig_relpath;
  const char *child_relpath;
  if (! ra_session)
      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
      SVN_ERR(svn_wc__node_get_repos_relpath(adjusted_path, wc_ctx,
                                             local_abspath, pool, pool));
      return SVN_NO_ERROR;
    }

  /* Now deal with the repos-repos and repos-wc diff cases.
   * We need to make PATH appear as a child of ORIG_TARGET.
   * ORIG_TARGET is either a URL or a path to a working copy. First,
   * find out what ORIG_TARGET looks like relative to the repository root.*/
  if (svn_path_is_url(orig_target))
    SVN_ERR(svn_ra_get_path_relative_to_root(ra_session,
                                             &orig_relpath,
                                             orig_target, pool));
  else
    {
      const char *orig_abspath;
      SVN_ERR(svn_dirent_get_absolute(&orig_abspath, orig_target, pool));
      SVN_ERR(svn_wc__node_get_repos_relpath(&orig_relpath, wc_ctx,
                                             orig_abspath, pool, pool));
    }
  /* PATH is either a child of the working copy involved in the diff (in
   * the repos-wc diff case), or it's a relative path we can readily use
   * (in either of the repos-repos and repos-wc diff cases). */
  child_relpath = NULL;
  if (wc_root_abspath)
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
      child_relpath = svn_dirent_is_child(wc_root_abspath, local_abspath, pool);
  if (child_relpath == NULL)
    child_relpath = path;
  *adjusted_path = svn_relpath_join(orig_relpath, child_relpath, pool);
/* Adjust PATH, ORIG_PATH_1 and ORIG_PATH_2, representing the changed file
 * and the two original targets passed to the diff command, to handle the
 * directory the diff target should be considered relative to. All
 * allocations are done in POOL. */
adjust_paths_for_diff_labels(const char **path,
                             apr_pool_t *pool)
  apr_size_t len;
  const char *new_path = *path;
  /* ### Holy cow.  Due to anchor/target weirdness, we can't
     simply join diff_cmd_baton->orig_path_1 with path, ditto for
     orig_path_2.  That will work when they're directory URLs, but
     not for file URLs.  Nor can we just use anchor1 and anchor2
     from do_diff(), at least not without some more logic here.
     What a nightmare.
     For now, to distinguish the two paths, we'll just put the
     unique portions of the original targets in parentheses after
     the received path, with ellipses for handwaving.  This makes
     the labels a bit clumsy, but at least distinctive.  Better
     solutions are possible, they'll just take more thought. */
  len = strlen(svn_dirent_get_longest_ancestor(new_path1, new_path2, pool));
  new_path1 = new_path1 + len;
  new_path2 = new_path2 + len;
    new_path1 = apr_psprintf(pool, "%s", new_path);
    new_path1 = apr_psprintf(pool, "%s\t(...%s)", new_path, new_path1);
    new_path1 = apr_psprintf(pool, "%s\t(.../%s)", new_path, new_path1);
    new_path2 = apr_psprintf(pool, "%s", new_path);
    new_path2 = apr_psprintf(pool, "%s\t(...%s)", new_path, new_path2);
    new_path2 = apr_psprintf(pool, "%s\t(.../%s)", new_path, new_path2);

  if (relative_to_dir)
    {
      /* Possibly adjust the paths shown in the output (see issue #2723). */
      const char *child_path = svn_dirent_is_child(relative_to_dir, new_path,
                                                   pool);

      if (child_path)
        new_path = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, new_path))
        new_path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(new_path, relative_to_dir);

      child_path = svn_dirent_is_child(relative_to_dir, new_path1, pool);

      if (child_path)
        new_path1 = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, new_path1))
        new_path1 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(new_path1, relative_to_dir);

      child_path = svn_dirent_is_child(relative_to_dir, new_path2, pool);
      if (child_path)
        new_path2 = child_path;
      else if (!svn_path_compare_paths(relative_to_dir, new_path2))
        new_path2 = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(new_path2, relative_to_dir);
    }
  *path = new_path;
  if (revnum != SVN_INVALID_REVNUM)
  else
                             const char *copyfrom_path, const char *path,
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, result_pool,
                                      "copy from %s%s", copyfrom_path,
                                      APR_EOL_STR));
 * revisions being diffed. COPYFROM_PATH indicates where the diffed item
 * was copied from. RA_SESSION and WC_CTX are used to adjust paths in the
 * headers to be relative to the repository root.
 * WC_ROOT_ABSPATH is the absolute path to the root directory of a working
 * copy involved in a repos-wc diff, and may be NULL.
                      svn_ra_session_t *ra_session,
                      svn_wc_context_t *wc_ctx,
                      const char *wc_root_abspath,
                                           copyfrom_path, repos_relpath2,
   to FILE.   Of course, the apr_file_t will probably be the 'outfile'
   passed to svn_client_diff5, which is probably stdout.
   WC_ROOT_ABSPATH is the absolute path to the root directory of a working
   copy involved in a repos-wc diff, and may be NULL. */
                   const char *path,
                   apr_file_t *file,
                   svn_ra_session_t *ra_session,
                   const char *wc_root_abspath,
                   apr_pool_t *pool)
  int i;
  const char *path1 = apr_pstrdup(pool, orig_path1);
  const char *path2 = apr_pstrdup(pool, orig_path2);
      SVN_ERR(adjust_relative_to_repos_root(&path1, path, orig_path1,
                                            ra_session, wc_ctx,
                                            wc_root_abspath,
                                            pool));
      SVN_ERR(adjust_relative_to_repos_root(&path2, path, orig_path2,
                                            ra_session, wc_ctx,
                                            wc_root_abspath,
                                            pool));
  if (path[0] == '\0')
    path = apr_psprintf(pool, ".");
      const char *adjusted_path1 = apr_pstrdup(pool, path1);
      const char *adjusted_path2 = apr_pstrdup(pool, path2);
      SVN_ERR(adjust_paths_for_diff_labels(&path, &adjusted_path1,
                                           &adjusted_path2,
                                           relative_to_dir, pool));

      label1 = diff_label(adjusted_path1, rev1, pool);
      label2 = diff_label(adjusted_path2, rev2, pool);
      SVN_ERR(file_printf_from_utf8(file, encoding,
                                    "Index: %s" APR_EOL_STR
                                    "%s" APR_EOL_STR,
                                    path, equal_string));
        {
          svn_stream_t *os;

          os = svn_stream_from_aprfile2(file, TRUE, pool);
          SVN_ERR(print_git_diff_header(os, &label1, &label2,
                                        svn_diff_op_modified,
                                        path1, path2, rev1, rev2, NULL,
                                        encoding, ra_session, wc_ctx,
                                        wc_root_abspath, pool));
          SVN_ERR(svn_stream_close(os));
        }

      SVN_ERR(file_printf_from_utf8(file, encoding,
                                          "--- %s" APR_EOL_STR
                                          "+++ %s" APR_EOL_STR,
                                          label1,
                                          label2));
  SVN_ERR(file_printf_from_utf8(file, encoding,
                                _("%sProperty changes on: %s%s"),
                                APR_EOL_STR,
                                use_git_diff_format ? path1 : path,
                                APR_EOL_STR));

  SVN_ERR(file_printf_from_utf8(file, encoding, "%s" APR_EOL_STR,
                                under_string));

  for (i = 0; i < propchanges->nelts; i++)
    {
      const char *action;
      const svn_string_t *original_value;
      const svn_prop_t *propchange =
        &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      if (original_props)
        original_value = apr_hash_get(original_props,
                                      propchange->name, APR_HASH_KEY_STRING);
      else
        original_value = NULL;

      /* If the property doesn't exist on either side, or if it exists
         with the same value, skip it.  */
      if ((! (original_value || propchange->value))
          || (original_value && propchange->value
              && svn_string_compare(original_value, propchange->value)))
        continue;

      if (! original_value)
        action = "Added";
      else if (! propchange->value)
        action = "Deleted";
      else
        action = "Modified";
      SVN_ERR(file_printf_from_utf8(file, encoding, "%s: %s%s", action,
                                    propchange->name, APR_EOL_STR));
      if (strcmp(propchange->name, SVN_PROP_MERGEINFO) == 0)
        {
          const char *orig = original_value ? original_value->data : NULL;
          const char *val = propchange->value ? propchange->value->data : NULL;
          svn_error_t *err = display_mergeinfo_diff(orig, val, encoding,
                                                    file, pool);

          /* Issue #3896: If we can't pretty-print mergeinfo differences
             because invalid mergeinfo is present, then don't let the diff
             fail, just print the diff as any other property. */
          if (err && err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              svn_error_clear(err);
            }
          else
            {
              SVN_ERR(err);
              continue;
            }
        }
      {
        svn_stream_t *os = svn_stream_from_aprfile2(file, TRUE, pool);
        svn_diff_t *diff;
        svn_diff_file_options_t options = { 0 };
        const svn_string_t *tmp;
        const svn_string_t *orig;
        const svn_string_t *val;
        svn_boolean_t val_has_eol;

        /* The last character in a property is often not a newline.
           An eol character is appended to prevent the diff API to add a
           ' \ No newline at end of file' line. We add 
           ' \ No newline at end of property' manually if needed. */
        tmp = original_value ? original_value : svn_string_create("", pool);
        orig = maybe_append_eol(tmp, NULL, pool);

        tmp = propchange->value ? propchange->value :
                                  svn_string_create("", pool);
        val = maybe_append_eol(tmp, &val_has_eol, pool);

        SVN_ERR(svn_diff_mem_string_diff(&diff, orig, val, &options, pool));

        /* UNIX patch will try to apply a diff even if the diff header
         * is missing. It tries to be helpful by asking the user for a
         * target filename when it can't determine the target filename
         * from the diff header. But there usually are no files which
         * UNIX patch could apply the property diff to, so we use "##"
         * instead of "@@" as the default hunk delimiter for property diffs.
         * We also supress the diff header. */
        SVN_ERR(svn_diff_mem_string_output_unified2(os, diff, FALSE, "##",
                                           svn_dirent_local_style(path, pool),
                                           svn_dirent_local_style(path, pool),
                                           encoding, orig, val, pool));
        SVN_ERR(svn_stream_close(os));
        if (!val_has_eol)
          {
            const char *s = "\\ No newline at end of property" APR_EOL_STR;
            apr_size_t len = strlen(s);
            SVN_ERR(svn_stream_write(os, s, &len));
          }
      }
    }
struct diff_cmd_baton {
  apr_file_t *outfile;
  apr_file_t *errfile;
  /* The original targets passed to the diff command.  We may need
     these to construct distinctive diff labels when comparing the
     same relative path in the same revision, under different anchors
     (for example, when comparing a trunk against a branch). */
  const char *orig_path_1;
  const char *orig_path_2;

  /* These are the numeric representations of the revisions passed to
     svn_client_diff5, either may be SVN_INVALID_REVNUM.  We need these
     because some of the svn_wc_diff_callbacks4_t don't get revision
     arguments.

     ### Perhaps we should change the callback signatures and eliminate
     ### these?
  */
  svn_revnum_t revnum1;
  svn_revnum_t revnum2;

  /* Set this flag if you want diff_file_changed to output diffs
     unconditionally, even if the diffs are empty. */
  svn_boolean_t force_empty;

  svn_wc_context_t *wc_ctx;

  /* The RA session used during diffs involving the repository. */
  svn_ra_session_t *ra_session;
  /* During a repos-wc diff, this is the absolute path to the root
   * directory of the working copy involved in the diff. */
  const char *wc_root_abspath;
  /* The anchor to prefix before wc paths */
  const char *anchor;
  /* A hashtable using the visited paths as keys.
   * ### This is needed for us to know if we need to print a diff header for
   * ### a path that has property changes. */
  apr_hash_t *visited_paths;
};
/* A helper function that marks a path as visited. It copies PATH
 * into the correct pool before referencing it from the hash table. */
static void
mark_path_as_visited(struct diff_cmd_baton *diff_cmd_baton, const char *path)
{
  const char *p;
  p = apr_pstrdup(apr_hash_pool_get(diff_cmd_baton->visited_paths), path);
  apr_hash_set(diff_cmd_baton->visited_paths, p, APR_HASH_KEY_STRING, p);
}
diff_props_changed(svn_wc_notify_state_t *state,
                   svn_boolean_t *tree_conflicted,
                   const char *path,
                   svn_boolean_t dir_was_added,
                   void *diff_baton,
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  svn_boolean_t show_diff_header;
  if (apr_hash_get(diff_cmd_baton->visited_paths, path, APR_HASH_KEY_STRING))
    show_diff_header = FALSE;
  else
    show_diff_header = TRUE;

      /* We're using the revnums from the diff_cmd_baton since there's
      SVN_ERR(display_prop_diffs(props, original_props, path,
                                 diff_cmd_baton->orig_path_1,
                                 diff_cmd_baton->orig_path_2,
                                 diff_cmd_baton->revnum1,
                                 diff_cmd_baton->revnum2,
                                 diff_cmd_baton->header_encoding,
                                 diff_cmd_baton->outfile,
                                 diff_cmd_baton->relative_to_dir,
                                 diff_cmd_baton->use_git_diff_format,
                                 diff_cmd_baton->ra_session,
                                 diff_cmd_baton->wc_ctx,
                                 diff_cmd_baton->wc_root_abspath,

      /* We've printed the diff header so now we can mark the path as
       * visited. */
      if (show_diff_header)
        mark_path_as_visited(diff_cmd_baton, path);
  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_props_changed(svn_wc_notify_state_t *state,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       svn_boolean_t dir_was_added,
                       const apr_array_header_t *propchanges,
                       apr_hash_t *original_props,
                       void *diff_baton,
                       apr_pool_t *scratch_pool)
{
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);
  return svn_error_trace(diff_props_changed(state,
                                            tree_conflicted, path,
                                            dir_was_added,
                                            propchanges,
                                            original_props,
                                            diff_baton,
                                            scratch_pool));
}

/* Show differences between TMPFILE1 and TMPFILE2. PATH, REV1, and REV2 are
   used in the headers to indicate the file and revisions.  If either
   MIMETYPE1 or MIMETYPE2 indicate binary content, don't show a diff,
   but instead print a warning message. */
diff_content_changed(const char *path,
                     void *diff_baton)
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  int exitcode;
  apr_pool_t *subpool = svn_pool_create(diff_cmd_baton->pool);
  svn_stream_t *os;
  const char *rel_to_dir = diff_cmd_baton->relative_to_dir;
  apr_file_t *errfile = diff_cmd_baton->errfile;
  const char *path1, *path2;
  /* Get a stream from our output file. */
  os = svn_stream_from_aprfile2(diff_cmd_baton->outfile, TRUE, subpool);
  path1 = apr_pstrdup(subpool, diff_cmd_baton->orig_path_1);
  path2 = apr_pstrdup(subpool, diff_cmd_baton->orig_path_2);

  SVN_ERR(adjust_paths_for_diff_labels(&path, &path1, &path2,
                                       rel_to_dir, subpool));

  label1 = diff_label(path1, rev1, subpool);
  label2 = diff_label(path2, rev2, subpool);
  if (! diff_cmd_baton->force_binary && (mt1_binary || mt2_binary))
      SVN_ERR(svn_stream_printf_from_utf8
              (os, diff_cmd_baton->header_encoding, subpool,
               "Index: %s" APR_EOL_STR "%s" APR_EOL_STR, path, equal_string));
      SVN_ERR(svn_stream_printf_from_utf8
              (os, diff_cmd_baton->header_encoding, subpool,
               _("Cannot display: file marked as a binary type.%s"),
               APR_EOL_STR));

      if (mt1_binary && !mt2_binary)
        SVN_ERR(svn_stream_printf_from_utf8
                (os, diff_cmd_baton->header_encoding, subpool,
                 "svn:mime-type = %s" APR_EOL_STR, mimetype1));
      else if (mt2_binary && !mt1_binary)
        SVN_ERR(svn_stream_printf_from_utf8
                (os, diff_cmd_baton->header_encoding, subpool,
                 "svn:mime-type = %s" APR_EOL_STR, mimetype2));
      else if (mt1_binary && mt2_binary)
          if (strcmp(mimetype1, mimetype2) == 0)
            SVN_ERR(svn_stream_printf_from_utf8
                    (os, diff_cmd_baton->header_encoding, subpool,
                     "svn:mime-type = %s" APR_EOL_STR,
                     mimetype1));
          else
            SVN_ERR(svn_stream_printf_from_utf8
                    (os, diff_cmd_baton->header_encoding, subpool,
                     "svn:mime-type = (%s, %s)" APR_EOL_STR,
                     mimetype1, mimetype2));
      svn_pool_destroy(subpool);
  if (diff_cmd_baton->diff_cmd)
      SVN_ERR(svn_stream_printf_from_utf8
              (os, diff_cmd_baton->header_encoding, subpool,
               "Index: %s" APR_EOL_STR "%s" APR_EOL_STR, path, equal_string));
      /* Close the stream (flush) */
      SVN_ERR(svn_stream_close(os));
                               diff_cmd_baton->options.for_external.argv,
                               diff_cmd_baton->options.for_external.argc,
                               &exitcode, diff_cmd_baton->outfile, errfile,
                               diff_cmd_baton->diff_cmd, subpool));
      /* We have a printed a diff for this path, mark it as visited. */
      mark_path_as_visited(diff_cmd_baton, path);
                                   diff_cmd_baton->options.for_internal,
                                   subpool));
      if (svn_diff_contains_diffs(diff) || diff_cmd_baton->force_empty ||
          diff_cmd_baton->use_git_diff_format)
          SVN_ERR(svn_stream_printf_from_utf8
                  (os, diff_cmd_baton->header_encoding, subpool,
                   "Index: %s" APR_EOL_STR "%s" APR_EOL_STR,
                   path, equal_string));
          if (diff_cmd_baton->use_git_diff_format)
              const char *tmp_path1, *tmp_path2;
              SVN_ERR(adjust_relative_to_repos_root(
                         &tmp_path1, path, diff_cmd_baton->orig_path_1,
                         diff_cmd_baton->ra_session, diff_cmd_baton->wc_ctx,
                         diff_cmd_baton->wc_root_abspath, subpool));
              SVN_ERR(adjust_relative_to_repos_root(
                         &tmp_path2, path, diff_cmd_baton->orig_path_2,
                         diff_cmd_baton->ra_session, diff_cmd_baton->wc_ctx,
                         diff_cmd_baton->wc_root_abspath, subpool));
              SVN_ERR(print_git_diff_header(os, &label1, &label2, operation,
                                            tmp_path1, tmp_path2, rev1, rev2,
                                            diff_cmd_baton->header_encoding,
                                            diff_cmd_baton->ra_session,
                                            diff_cmd_baton->wc_ctx,
                                            diff_cmd_baton->wc_root_abspath,
                                            subpool));
          if (svn_diff_contains_diffs(diff) || diff_cmd_baton->force_empty)
            SVN_ERR(svn_diff_file_output_unified3
                    (os, diff, tmpfile1, tmpfile2, label1, label2,
                     diff_cmd_baton->header_encoding, rel_to_dir,
                     diff_cmd_baton->options.for_internal->show_c_function,
                     subpool));

          /* We have a printed a diff for this path, mark it as visited. */
          mark_path_as_visited(diff_cmd_baton, path);

      /* Close the stream (flush) */
      SVN_ERR(svn_stream_close(os));
  /* Destroy the subpool. */
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
diff_file_opened(svn_boolean_t *tree_conflicted,
                 svn_boolean_t *skip,
                 const char *path,
                 svn_revnum_t rev,
                 void *diff_baton,
                 apr_pool_t *scratch_pool)
{
/* An svn_wc_diff_callbacks4_t function. */
diff_file_changed(svn_wc_notify_state_t *content_state,
                  svn_wc_notify_state_t *prop_state,
                  svn_boolean_t *tree_conflicted,
                  const char *path,
                  const char *tmpfile1,
                  const char *tmpfile2,
                  svn_revnum_t rev1,
                  svn_revnum_t rev2,
                  const char *mimetype1,
                  const char *mimetype2,
                  apr_hash_t *original_props,
                  void *diff_baton,
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);
  if (tmpfile1)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, rev1, rev2,
                                 mimetype1, mimetype2,
                                 svn_diff_op_modified, NULL, diff_baton));
    SVN_ERR(diff_props_changed(prop_state, tree_conflicted,
                               path, FALSE, prop_changes,
                               original_props, diff_baton, scratch_pool));
  if (content_state)
    *content_state = svn_wc_notify_state_unknown;
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
/* An svn_wc_diff_callbacks4_t function. */
diff_file_added(svn_wc_notify_state_t *content_state,
                svn_wc_notify_state_t *prop_state,
                svn_boolean_t *tree_conflicted,
                const char *path,
                const char *tmpfile1,
                const char *tmpfile2,
                svn_revnum_t rev1,
                svn_revnum_t rev2,
                const char *mimetype1,
                const char *mimetype2,
                const char *copyfrom_path,
                svn_revnum_t copyfrom_revision,
                const apr_array_header_t *prop_changes,
                apr_hash_t *original_props,
                void *diff_baton,
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;

  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);

  /* We want diff_file_changed to unconditionally show diffs, even if
     the diff is empty (as would be the case if an empty file were
     added.)  It's important, because 'patch' would still see an empty
     diff and create an empty file.  It's also important to let the
     user see that *something* happened. */
  diff_cmd_baton->force_empty = TRUE;

  if (tmpfile1 && copyfrom_path)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, rev1, rev2,
                                 mimetype1, mimetype2,
                                 svn_diff_op_copied, copyfrom_path,
                                 diff_baton));
  else if (tmpfile1)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, rev1, rev2,
                                 mimetype1, mimetype2,
                                 svn_diff_op_added, NULL, diff_baton));
    SVN_ERR(diff_props_changed(prop_state, tree_conflicted,
                               path, FALSE, prop_changes,
                               original_props, diff_baton, scratch_pool));
  if (content_state)
    *content_state = svn_wc_notify_state_unknown;
  if (prop_state)
    *prop_state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  diff_cmd_baton->force_empty = FALSE;
/* An svn_wc_diff_callbacks4_t function. */
diff_file_deleted_with_diff(svn_wc_notify_state_t *state,
                            svn_boolean_t *tree_conflicted,
                            const char *path,
                            const char *tmpfile1,
                            const char *tmpfile2,
                            const char *mimetype1,
                            const char *mimetype2,
                            apr_hash_t *original_props,
                            void *diff_baton,
                            apr_pool_t *scratch_pool)
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);
  if (tmpfile1)
    SVN_ERR(diff_content_changed(path,
                                 tmpfile1, tmpfile2, diff_cmd_baton->revnum1,
                                 diff_cmd_baton->revnum2,
                                 mimetype1, mimetype2,
                                 svn_diff_op_deleted, NULL, diff_baton));
  /* We don't list all the deleted properties. */
  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
diff_file_deleted_no_diff(svn_wc_notify_state_t *state,
                          svn_boolean_t *tree_conflicted,
                          const char *path,
                          const char *tmpfile1,
                          const char *tmpfile2,
                          const char *mimetype1,
                          const char *mimetype2,
                          apr_hash_t *original_props,
                          void *diff_baton,
                          apr_pool_t *scratch_pool)
  struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);
  if (state)
    *state = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return file_printf_from_utf8
          (diff_cmd_baton->outfile,
           diff_cmd_baton->header_encoding,
           "Index: %s (deleted)" APR_EOL_STR "%s" APR_EOL_STR,
           path, equal_string);
/* An svn_wc_diff_callbacks4_t function. */
diff_dir_added(svn_wc_notify_state_t *state,
               svn_boolean_t *tree_conflicted,
               svn_boolean_t *skip,
               svn_boolean_t *skip_children,
               const char *path,
               svn_revnum_t rev,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void *diff_baton,
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/
  /* Do nothing. */
  return SVN_NO_ERROR;
/* An svn_wc_diff_callbacks4_t function. */
diff_dir_deleted(svn_wc_notify_state_t *state,
                 svn_boolean_t *tree_conflicted,
                 const char *path,
                 void *diff_baton,
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/
  /* Do nothing. */
  return SVN_NO_ERROR;
}
/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_opened(svn_boolean_t *tree_conflicted,
                svn_boolean_t *skip,
                svn_boolean_t *skip_children,
                const char *path,
                svn_revnum_t rev,
                void *diff_baton,
                apr_pool_t *scratch_pool)
{
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/
  /* Do nothing. */
/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_closed(svn_wc_notify_state_t *contentstate,
                svn_wc_notify_state_t *propstate,
                svn_boolean_t *tree_conflicted,
                const char *path,
                svn_boolean_t dir_was_added,
                void *diff_baton,
                apr_pool_t *scratch_pool)
{
  /*struct diff_cmd_baton *diff_cmd_baton = diff_baton;
  if (diff_cmd_baton->anchor)
    path = svn_dirent_join(diff_cmd_baton->anchor, path, scratch_pool);*/

  /* Do nothing. */

  return SVN_NO_ERROR;
}


      1. path is not an URL and start_revision != end_revision
      2. path is not an URL and start_revision == end_revision
      3. path is an URL and start_revision != end_revision
      4. path is an URL and start_revision == end_revision
      5. path is not an URL and no revisions given
   other.  When path is an URL there is no working copy. Thus


/* Helper function: given a working-copy ABSPATH_OR_URL, return its
   associated url in *URL, allocated in RESULT_POOL.  If ABSPATH_OR_URL is
   *already* a URL, that's fine, return ABSPATH_OR_URL allocated in
   RESULT_POOL.

   Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
convert_to_url(const char **url,
               svn_wc_context_t *wc_ctx,
               const char *abspath_or_url,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  if (svn_path_is_url(abspath_or_url))
    {
      *url = apr_pstrdup(result_pool, abspath_or_url);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__node_get_url(url, wc_ctx, abspath_or_url,
                               result_pool, scratch_pool));
  if (! *url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Path '%s' has no URL"),
                             svn_dirent_local_style(abspath_or_url,
                                                    scratch_pool));
  return SVN_NO_ERROR;
}

/** Check if paths PATH1 and PATH2 are urls and if the revisions REVISION1
 *  and REVISION2 are local. If PEG_REVISION is not unspecified, ensure that
 *  at least one of the two revisions is non-local.
 *  If PATH1 can only be found in the repository, set *IS_REPOS1 to TRUE.
 *  If PATH2 can only be found in the repository, set *IS_REPOS2 to TRUE. */
            const char *path1,
            const char *path2,
  /* Revisions can be said to be local or remote.  BASE and WORKING,
     for example, are local.  */
  if (peg_revision->kind != svn_opt_revision_unspecified)
    {
      if (is_local_rev1 && is_local_rev2)
        return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                _("At least one revision must be non-local "
                                  "for a pegged diff"));
      *is_repos1 = ! is_local_rev1 || svn_path_is_url(path1);
      *is_repos2 = ! is_local_rev2 || svn_path_is_url(path2);
    }
  else
    {
      /* Working copy paths with non-local revisions get turned into
         URLs.  We don't do that here, though.  We simply record that it
         needs to be done, which is information that helps us choose our
         diff helper function.  */
      *is_repos1 = ! is_local_rev1 || svn_path_is_url(path1);
      *is_repos2 = ! is_local_rev2 || svn_path_is_url(path2);
    }
/* Resolve PATH_OR_URL@PEG_REVISION to a possibly different *RESOLVED_URL
 * which the corresponding object has in REVISION. If the object has no
 * location in REVISION, set *RESOLVED_URL to NULL. */
static svn_error_t *
resolve_pegged_diff_target_url(const char **resolved_url,
                               svn_ra_session_t *ra_session,
                               const char *path_or_url,
                               const svn_opt_revision_t *peg_revision,
                               const svn_opt_revision_t *revision,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *scratch_pool)
{
  svn_opt_revision_t *start_rev_ignore, *end_rev_ignore;
  const char *end_url_ignore;
  static const svn_opt_revision_t unspecified_rev =
    { svn_opt_revision_unspecified, { 0 } };
  svn_error_t *err;

  /* Check if the PATH_OR_URL exists at REVISION. */
  err = svn_client__repos_locations(resolved_url, &start_rev_ignore,
                                    &end_url_ignore, &end_rev_ignore,
                                    ra_session,
                                    path_or_url,
                                    peg_revision,
                                    revision,
                                    &unspecified_rev,
                                    ctx, scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES ||
          err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          *resolved_url = NULL;
        }
      else
        return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}

/** Prepare a repos repos diff between PATH1 and PATH2@PEG_REVISION,
 * in the revision range REVISION1:REVISION2.
 * Set *BASE_PATH corresponding to the URL opened in the new *RA_SESSION
 * which is pointing at *ANCHOR1.
                         const char **base_path,
                         const char *path1,
                         const char *path2,
  const char *path2_abspath;
  const char *path1_abspath;
  if (!svn_path_is_url(path2))
    SVN_ERR(svn_dirent_get_absolute(&path2_abspath, path2,
                                    pool));
  else
    path2_abspath = path2;
  if (!svn_path_is_url(path1))
    SVN_ERR(svn_dirent_get_absolute(&path1_abspath, path1,
                                    pool));
  else
    path1_abspath = path1;

  /* Figure out URL1 and URL2. */
  SVN_ERR(convert_to_url(url1, ctx->wc_ctx, path1_abspath,
                         pool, pool));
  SVN_ERR(convert_to_url(url2, ctx->wc_ctx, path2_abspath,
                         pool, pool));

  /* We need exactly one BASE_PATH, so we'll let the BASE_PATH
     calculated for PATH2 override the one for PATH1 (since the diff
     will be "applied" to URL2 anyway). */
  *base_path = NULL;
  if (strcmp(*url1, path1) != 0)
    *base_path = path1;
  if (strcmp(*url2, path2) != 0)
    *base_path = path2;

  SVN_ERR(svn_client__open_ra_session_internal(ra_session, NULL, *url2,
                                               NULL, NULL, FALSE,
                                               TRUE, ctx, pool));
  if (peg_revision->kind != svn_opt_revision_unspecified)
      const char *resolved_url1;
      const char *resolved_url2;

      SVN_ERR(resolve_pegged_diff_target_url(&resolved_url2, *ra_session,
                                             path2, peg_revision,
                                             revision2, ctx, pool));

      SVN_ERR(svn_ra_reparent(*ra_session, *url1, pool));
      SVN_ERR(resolve_pegged_diff_target_url(&resolved_url1, *ra_session,
                                             path1, peg_revision,
                                             revision1, ctx, pool));

      /* Either or both URLs might have changed as a result of resolving
       * the PATH_OR_URL@PEG_REVISION's history. If only one of the URLs
       * could be resolved, use the same URL for URL1 and URL2, so we can
       * show diff that 'adds' the object (see issue #4153). */
      if (resolved_url2)
          *url2 = resolved_url2;
          if (!resolved_url1)
            *url1 = resolved_url2;
      if (resolved_url1)
          *url1 = resolved_url1;
          if (!resolved_url2)
            *url2 = resolved_url1;
      /* Reparent the session, since *URL2 might have changed as a result
         the above call. */
      SVN_ERR(svn_ra_reparent(*ra_session, *url2, pool));
  SVN_ERR(svn_client__get_revision_number(rev2, NULL, ctx->wc_ctx,
           (path2 == *url2) ? NULL : path2_abspath,
           *ra_session, revision2, pool));
  SVN_ERR(svn_client__get_revision_number(rev1, NULL, ctx->wc_ctx,
           (strcmp(path1, *url1) == 0) ? NULL : path1_abspath,
           *ra_session, revision1, pool));
                                 _("Diff targets '%s and '%s' were not found "
  /* If one of the targets is a file, use the parent directory as anchor. */
  if (*kind1 == svn_node_file || *kind2 == svn_node_file)
      if (*base_path)
        *base_path = svn_dirent_dirname(*base_path, pool);
   This function is really svn_client_diff5().  If you read the public
   API description for svn_client_diff5(), it sounds quite Grand.  It
   pigeonholed into one of these three use-cases, we currently bail
   with a friendly apology.
   Perhaps someday a brave soul will truly make svn_client_diff5
                          _("Sorry, svn_client_diff5 was called in a way "

   All other options are the same as those passed to svn_client_diff5(). */
diff_wc_wc(const char *path1,
           svn_boolean_t show_copies_as_adds,
           svn_boolean_t use_git_diff_format,
           const svn_wc_diff_callbacks4_t *callbacks,
           struct diff_cmd_baton *callback_baton,
           apr_pool_t *pool)
  svn_error_t *err;
  svn_node_kind_t kind;
  SVN_ERR(svn_dirent_get_absolute(&abspath1, path1, pool));
    return unsupported_diff_error
      (svn_error_create
       (SVN_ERR_INCORRECT_PARAMS, NULL,
        _("Only diffs between a path's text-base "
          "and its working files are supported at this time")));

  /* Resolve named revisions to real numbers. */
  err = svn_client__get_revision_number(&callback_baton->revnum1, NULL,
                                        ctx->wc_ctx, abspath1, NULL,
                                        revision1, pool);

  /* In case of an added node, we have no base rev, and we show a revision
   * number of 0. Note that this code is currently always asking for
   * svn_opt_revision_base.
   * ### TODO: get rid of this 0 for added nodes. */
  if (err && (err->apr_err == SVN_ERR_CLIENT_BAD_REVISION))
    {
      svn_error_clear(err);
      callback_baton->revnum1 = 0;
    }
  else
    SVN_ERR(err);

  callback_baton->revnum2 = SVN_INVALID_REVNUM;  /* WC */

  SVN_ERR(svn_wc_read_kind(&kind, ctx->wc_ctx, abspath1, FALSE, pool));

  if (kind != svn_node_dir)
    callback_baton->anchor = svn_dirent_dirname(path1, pool);
  else
    callback_baton->anchor = path1;

  SVN_ERR(svn_wc_diff6(ctx->wc_ctx,
                       abspath1,
                       callbacks, callback_baton,
                       depth,
                       ignore_ancestry, show_copies_as_adds,
                       use_git_diff_format, changelists,
                       ctx->cancel_func, ctx->cancel_baton,
                       pool));
  return SVN_NO_ERROR;
}

/* Create an array of regular properties in PROP_HASH, filtering entry-props
 * and wc-props. Allocate the returned array in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static apr_array_header_t *
make_regular_props_array(apr_hash_t *prop_hash,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *regular_props;
  apr_hash_index_t *hi;

  regular_props = apr_array_make(result_pool, 0, sizeof(svn_prop_t));
  for (hi = apr_hash_first(scratch_pool, prop_hash); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *value = svn__apr_hash_index_val(hi);
      svn_prop_kind_t prop_kind = svn_property_kind(NULL, name);

      if (prop_kind == svn_prop_regular_kind)
        {
          svn_prop_t *prop = apr_palloc(scratch_pool, sizeof(svn_prop_t));

          prop->name = name;
          prop->value = value;
          APR_ARRAY_PUSH(regular_props, svn_prop_t) = *prop;
        }
    }
  return regular_props;
}

/* Create a hash of regular properties from PROP_HASH, filtering entry-props
 * and wc-props. Allocate the returned hash in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static apr_hash_t *
make_regular_props_hash(apr_hash_t *prop_hash,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_hash_t *regular_props;
  apr_hash_index_t *hi;

  regular_props = apr_hash_make(result_pool);
  for (hi = apr_hash_first(scratch_pool, prop_hash); hi;
       hi = apr_hash_next(hi))
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *value = svn__apr_hash_index_val(hi);
      svn_prop_kind_t prop_kind = svn_property_kind(NULL, name);
      if (prop_kind == svn_prop_regular_kind)
        apr_hash_set(regular_props, name, APR_HASH_KEY_STRING, value);
    }
  return regular_props;
}

/* Handle an added or deleted diff target file for a repos<->repos diff.
 *
 * Using the provided diff CALLBACKS and the CALLBACK_BATON, show the file
 * TARGET@PEG_REVISION as added or deleted, depending on SHOW_DELETION.
 * TARGET is a path relative to RA_SESSION's URL.
 * REV1 and REV2 are the revisions being compared.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
diff_repos_repos_added_or_deleted_file(const char *target,
                                       svn_revnum_t peg_revision,
                                       svn_revnum_t rev1,
                                       svn_revnum_t rev2,
                                       svn_boolean_t show_deletion,
                                      const char *empty_file,
                                       const svn_wc_diff_callbacks4_t
                                         *callbacks,
                                       struct diff_cmd_baton *callback_baton,
                                       svn_ra_session_t *ra_session,
                                       apr_pool_t *scratch_pool)
{
  const char *file_abspath;
  svn_stream_t *content;
  apr_hash_t *prop_hash;
  svn_string_t *mimetype;

  SVN_ERR(svn_stream_open_unique(&content, &file_abspath, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  SVN_ERR(svn_ra_get_file(ra_session, target, peg_revision, content, NULL,
                          &prop_hash, scratch_pool));
  SVN_ERR(svn_stream_close(content));

  mimetype = apr_hash_get(prop_hash, SVN_PROP_MIME_TYPE, APR_HASH_KEY_STRING);

  if (show_deletion)
    {
      SVN_ERR(callbacks->file_deleted(NULL, NULL,
                                      target, file_abspath, empty_file,
                                      mimetype ? mimetype->data : NULL,
                                      NULL,
                                      make_regular_props_hash(
                                        prop_hash, scratch_pool, scratch_pool),
                                      callback_baton, scratch_pool));
    }
  else
    {
      SVN_ERR(callbacks->file_added(NULL, NULL, NULL,
                                    target, empty_file, file_abspath,
                                    rev1, rev2, NULL,
                                    mimetype ? mimetype->data : NULL,
                                    NULL, SVN_INVALID_REVNUM,
                                    make_regular_props_array(prop_hash,
                                                             scratch_pool,
                                                             scratch_pool),
                                    NULL, callback_baton, scratch_pool));
    }
    
  return SVN_NO_ERROR;
}

/* Handle an added or deleted diff target directory for a repos<->repos diff.
 *
 * Using the provided diff CALLBACKS and the CALLBACK_BATON, show the
 * directory TARGET@PEG_REVISION, and all of its children, as added or deleted,
 * depending on SHOW_DELETION. TARGET is a path relative to RA_SESSION's URL.
 * REV1 and REV2 are the revisions being compared.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
diff_repos_repos_added_or_deleted_dir(const char *target,
                                      svn_revnum_t revision,
                                      svn_revnum_t rev1,
                                      svn_revnum_t rev2,
                                      svn_boolean_t show_deletion,
                                      const char *empty_file,
                                      const svn_wc_diff_callbacks4_t
                                        *callbacks,
                                      struct diff_cmd_baton *callback_baton,
                                      svn_ra_session_t *ra_session,
                                      apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_hash_t *props;
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;

  SVN_ERR(svn_ra_get_dir2(ra_session, &dirents, NULL, &props,
                          target, revision, SVN_DIRENT_KIND,
                          scratch_pool));

  if (show_deletion)
    SVN_ERR(callbacks->dir_deleted(NULL, NULL, target, callback_baton,
                                   scratch_pool));
  else
    SVN_ERR(callbacks->dir_added(NULL, NULL, NULL, NULL,
                                 target, revision,
                                 NULL, SVN_INVALID_REVNUM,
                                 callback_baton, scratch_pool));
  if (props)
    {
      if (show_deletion)
        SVN_ERR(callbacks->dir_props_changed(NULL, NULL, target, FALSE,
                                             apr_array_make(scratch_pool, 0,
                                                            sizeof(svn_prop_t)),
                                             make_regular_props_hash(
                                               props, scratch_pool,
                                               scratch_pool),
                                             callback_baton, scratch_pool));
        SVN_ERR(callbacks->dir_props_changed(NULL, NULL, target, TRUE,
                                             make_regular_props_array(
                                               props, scratch_pool,
                                               scratch_pool),
                                             NULL,
                                             callback_baton, scratch_pool));
    }

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_dirent_t *dirent = svn__apr_hash_index_val(hi);
      const char *child_target;

      svn_pool_clear(iterpool);

      child_target = svn_relpath_join(target, name, iterpool);

      if (dirent->kind == svn_node_dir)
        SVN_ERR(diff_repos_repos_added_or_deleted_dir(child_target,
                                                      revision, rev1, rev2,
                                                      show_deletion,
                                                      empty_file,
                                                      callbacks,
                                                      callback_baton,
                                                      ra_session,
                                                      iterpool));
      else if (dirent->kind == svn_node_file)
        SVN_ERR(diff_repos_repos_added_or_deleted_file(child_target,
                                                       revision, rev1, rev2,
                                                       show_deletion,
                                                       empty_file,
                                                       callbacks,
                                                       callback_baton,
                                                       ra_session,
                                                       iterpool));
    }
  svn_pool_destroy(iterpool);

  if (!show_deletion)
    SVN_ERR(callbacks->dir_closed(NULL, NULL, NULL, target, TRUE,
                                  callback_baton, scratch_pool));

  return SVN_NO_ERROR;
}


/* Handle an added or deleted diff target for a repos<->repos diff.
 *
 * Using the provided diff CALLBACKS and the CALLBACK_BATON, show
 * TARGET@PEG_REVISION, and all of its children, if any, as added or deleted.
 * TARGET is a path relative to RA_SESSION's URL.
 * REV1 and REV2 are the revisions being compared.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
diff_repos_repos_added_or_deleted_target(const char *target1,
                                         const char *target2,
                                         svn_revnum_t rev1,
                                         svn_revnum_t rev2,
                                         svn_node_kind_t kind1,
                                         svn_node_kind_t kind2,
                                         const svn_wc_diff_callbacks4_t
                                           *callbacks,
                                         struct diff_cmd_baton *callback_baton,
                                         svn_ra_session_t *ra_session,
                                         apr_pool_t *scratch_pool)
{
  const char *existing_target;
  svn_revnum_t existing_rev;
  svn_node_kind_t existing_kind;
  svn_boolean_t show_deletion;
  const char *empty_file;

  SVN_ERR_ASSERT(kind1 == svn_node_none || kind2 == svn_node_none);

  /* Are we showing an addition or deletion? */
  show_deletion = (kind2 == svn_node_none);

  /* Which target is being added/deleted? Is it a file or a directory? */
  if (show_deletion)
    {
      existing_target = target1;
      existing_rev = rev1;
      existing_kind = kind1;
    }
  else
    {
      existing_target = target2;
      existing_rev = rev2;
      existing_kind = kind2;
    }

  /* All file content will be diffed against the empty file. */
  SVN_ERR(svn_io_open_unique_file3(NULL, &empty_file, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   scratch_pool, scratch_pool));

  if (existing_kind == svn_node_file)
    {
      /* Get file content and show a diff against the empty file. */
      SVN_ERR(diff_repos_repos_added_or_deleted_file(existing_target,
                                                     existing_rev,
                                                     rev1, rev2,
                                                     show_deletion,
                                                     empty_file,
                                                     callbacks,
                                                     callback_baton,
                                                     ra_session,
                                                     scratch_pool));
    }
  else
    {
      /* Walk the added/deleted tree and show a diff for each child. */
      SVN_ERR(diff_repos_repos_added_or_deleted_dir(existing_target,
                                                    existing_rev,
                                                    rev1, rev2,
                                                    show_deletion,
                                                    empty_file,
                                                    callbacks,
                                                    callback_baton,
                                                    ra_session,
                                                    scratch_pool));
   PATH1 and PATH2 may be either URLs or the working copy paths.
   If PEG_REVISION is specified, PATH2 is the path at the peg revision,
   history from PATH2.
   All other options are the same as those passed to svn_client_diff5(). */
diff_repos_repos(const svn_wc_diff_callbacks4_t *callbacks,
                 struct diff_cmd_baton *callback_baton,
                 svn_client_ctx_t *ctx,
                 const char *path1,
                 const char *path2,
                 apr_pool_t *pool)
  const char *base_path;
  SVN_ERR(diff_prepare_repos_repos(&url1, &url2, &base_path, &rev1, &rev2,
                                   ctx, path1, path2,
                                   pool));
  /* Get actual URLs. */
  callback_baton->orig_path_1 = url1;
  callback_baton->orig_path_2 = url2;
  /* Get numeric revisions. */
  callback_baton->revnum1 = rev1;
  callback_baton->revnum2 = rev2;
  callback_baton->ra_session = ra_session;
  callback_baton->anchor = base_path;
  if (kind1 == svn_node_none || kind2 == svn_node_none)
      /* One side of the diff does not exist.
       * Walk the tree that does exist, showing a series of additions
       * or deletions. */
      SVN_ERR(diff_repos_repos_added_or_deleted_target(target1, target2,
                                                       rev1, rev2,
                                                       kind1, kind2,
                                                       callbacks,
                                                       callback_baton,
                                                       ra_session,
                                                       pool));
      return SVN_NO_ERROR;
    }
  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used during the editor calls to fetch file
     contents.  */
  SVN_ERR(svn_client__open_ra_session_internal(&extra_ra_session, NULL,
                                               anchor1, NULL, NULL, FALSE,
                                               TRUE, ctx, pool));
  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */
  SVN_ERR(svn_client__get_diff_editor(
                &diff_editor, &diff_edit_baton,
                NULL, "", depth,
                extra_ra_session, rev1, TRUE, FALSE,
                callbacks, callback_baton,
                ctx->cancel_func, ctx->cancel_baton,
                NULL /* no notify_func */, NULL /* no notify_baton */,
                pool, pool));
  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3
          (ra_session, &reporter, &reporter_baton, rev2, target1,
           depth, ignore_ancestry, TRUE,
           url2, diff_editor, diff_edit_baton, pool));
  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL,
                             pool));
  return reporter->finish_report(reporter_baton, pool);
}
/* Using CALLBACKS, show a REPOS->WC diff for a file TARGET, which in the
 * working copy is at FILE2_ABSPATH. KIND1 is the node kind of the repository
 * target (either svn_node_file or svn_node_none). REV is the revision the
 * working file is diffed against. RA_SESSION points at the URL of the file
 * in the repository and is used to get the file's repository-version content,
 * if necessary. If DIFF_WITH_BASE is set, diff against the BASE version of
 * the local file instead of WORKING.
 * The other parameters are as in diff_repos_wc(). */
static svn_error_t *
diff_repos_wc_file_target(const char *target,
                          const char *file2_abspath,
                          svn_node_kind_t kind1,
                          svn_revnum_t rev,
                          svn_boolean_t reverse,
                          svn_boolean_t show_copies_as_adds,
                          svn_boolean_t diff_with_base,
                          const svn_wc_diff_callbacks4_t *callbacks,
                          void *callback_baton,
                          svn_ra_session_t *ra_session,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool)
{
  const char *file1_abspath;
  svn_stream_t *file1_content;
  svn_stream_t *file2_content;
  apr_hash_t *file1_props = NULL;
  apr_hash_t *file2_props;
  svn_boolean_t is_copy = FALSE;
  svn_string_t *mimetype1, *mimetype2;

  /* Get content and props of file 1 (the remote file). */
  SVN_ERR(svn_stream_open_unique(&file1_content, &file1_abspath, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  if (kind1 == svn_node_file)
      if (show_copies_as_adds)
        SVN_ERR(svn_wc__node_get_origin(&is_copy, 
                                        NULL, NULL, NULL, NULL, NULL,
                                        ctx->wc_ctx, file2_abspath,
                                        FALSE, scratch_pool, scratch_pool));
      /* If showing copies as adds, diff against the empty file. */
      if (!(show_copies_as_adds && is_copy))
        SVN_ERR(svn_ra_get_file(ra_session, "", rev, file1_content,
                                NULL, &file1_props, scratch_pool));
  SVN_ERR(svn_stream_close(file1_content));
  /* Get content and props of file 2 (the local file). */
  if (diff_with_base)
      svn_stream_t *pristine_content;
      SVN_ERR(svn_wc_get_pristine_props(&file2_props, ctx->wc_ctx,
                                        file2_abspath, scratch_pool,
                                        scratch_pool));
      /* ### We need a filename, but this API returns an opaque stream.
       * ### This requires us to copy to a temporary file. Maybe libsvn_wc
       * ### should also provide an API that returns a path to a file that
       * ### contains pristine content, possibly temporary? */
      SVN_ERR(svn_wc_get_pristine_contents2(&pristine_content,
                                            ctx->wc_ctx,
                                            file2_abspath,
                                            scratch_pool, scratch_pool));

      SVN_ERR(svn_stream_open_unique(&file2_content, &file2_abspath, NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     scratch_pool, scratch_pool));
      SVN_ERR(svn_stream_copy3(pristine_content, file2_content,
                               ctx->cancel_func, ctx->cancel_baton,
                               scratch_pool));
  else
    {
      apr_hash_t *keywords = NULL;
      svn_string_t *keywords_prop;
      svn_string_t *eol_prop;
      svn_subst_eol_style_t eol_style;
      const char *eol_str;

      SVN_ERR(svn_wc_prop_list2(&file2_props, ctx->wc_ctx, file2_abspath,
                                scratch_pool, scratch_pool));

      /* We might have to create a normalised version of the working file. */
      eol_prop = apr_hash_get(file2_props, SVN_PROP_EOL_STYLE,
                              APR_HASH_KEY_STRING);
      svn_subst_eol_style_from_value(&eol_style, &eol_str,
                                     eol_prop ? eol_prop->data : NULL);
      keywords_prop = apr_hash_get(file2_props, SVN_PROP_KEYWORDS,
                                   APR_HASH_KEY_STRING);
      if (keywords_prop)
        SVN_ERR(svn_subst_build_keywords2(&keywords, keywords_prop->data,
                                          NULL, NULL, 0, NULL,
                                          scratch_pool));
      if (svn_subst_translation_required(eol_style, eol_str,
                                         keywords, FALSE, TRUE))
        {
          svn_stream_t *working_content;
          svn_stream_t *normalized_content;
          if (eol_style == svn_subst_eol_style_native)
            eol_str = SVN_SUBST_NATIVE_EOL_STR;
          else if (! (eol_style == svn_subst_eol_style_fixed
                      || eol_style == svn_subst_eol_style_none))
            return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);
          SVN_ERR(svn_stream_open_readonly(&working_content, file2_abspath,
                                           scratch_pool, scratch_pool));
          /* Create a temporary file and copy normalised data into it. */
          SVN_ERR(svn_stream_open_unique(&file2_content, &file2_abspath, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));
          normalized_content = svn_subst_stream_translated(
                                 file2_content, eol_str,
                                 eol_style == svn_subst_eol_style_fixed,
                                 keywords, FALSE, scratch_pool);
          SVN_ERR(svn_stream_copy3(working_content, normalized_content,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   scratch_pool));
        }
    }

  mimetype1 = file1_props ? apr_hash_get(file1_props, SVN_PROP_MIME_TYPE,
                                         APR_HASH_KEY_STRING)
                          : NULL;
  mimetype2 = apr_hash_get(file2_props, SVN_PROP_MIME_TYPE,
                           APR_HASH_KEY_STRING);
  if (kind1 == svn_node_file && !(show_copies_as_adds && is_copy))
    {
      apr_array_header_t *propchanges;

      SVN_ERR(callbacks->file_opened(NULL, NULL, target,
                                     reverse ? SVN_INVALID_REVNUM : rev,
                                     callback_baton, scratch_pool));

      if (reverse)
        {
          SVN_ERR(svn_prop_diffs(&propchanges, file1_props, file2_props,
                                 scratch_pool));

          SVN_ERR(callbacks->file_changed(NULL, NULL, NULL, target,
                                          file2_abspath, file1_abspath,
                                          SVN_INVALID_REVNUM, rev,
                                          mimetype2 ? mimetype2->data : NULL,
                                          mimetype1 ? mimetype1->data : NULL,
                                          propchanges, file2_props,
                                          callback_baton, scratch_pool));
        }
      else
        {
          SVN_ERR(svn_prop_diffs(&propchanges, file2_props, file1_props,
                                 scratch_pool));

          SVN_ERR(callbacks->file_changed(NULL, NULL, NULL, target,
                                          file1_abspath, file2_abspath,
                                          rev, SVN_INVALID_REVNUM,
                                          mimetype1 ? mimetype1->data : NULL,
                                          mimetype2 ? mimetype2->data : NULL,
                                          propchanges, file1_props,
                                          callback_baton, scratch_pool));
        }
    }
  else
    {
      if (reverse)
        {
          SVN_ERR(callbacks->file_deleted(NULL, NULL,
                                          target, file2_abspath, file1_abspath,
                                          mimetype2 ? mimetype2->data : NULL,
                                          NULL,
                                          make_regular_props_hash(
                                            file2_props, scratch_pool,
                                            scratch_pool),
                                          callback_baton, scratch_pool));
        }
      else
        {
          SVN_ERR(callbacks->file_added(NULL, NULL, NULL, target,
                                        file1_abspath, file2_abspath,
                                        rev, SVN_INVALID_REVNUM,
                                        NULL,
                                        mimetype2 ? mimetype2->data : NULL,
                                        NULL, SVN_INVALID_REVNUM,
                                        make_regular_props_array(
                                          file2_props, scratch_pool,
                                          scratch_pool),
                                        NULL,
                                        callback_baton, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
   PATH1 may be either a URL or a working copy path.  PATH2 is a
   If PEG_REVISION is specified, then PATH1 is the path in the peg
   All other options are the same as those passed to svn_client_diff5(). */
diff_repos_wc(const char *path1,
              svn_boolean_t show_copies_as_adds,
              svn_boolean_t use_git_diff_format,
              const svn_wc_diff_callbacks4_t *callbacks,
              struct diff_cmd_baton *callback_baton,
              apr_pool_t *pool)
  const char *url1, *anchor, *anchor_url, *target;
  svn_revnum_t rev;
  const char *abspath1;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  if (!svn_path_is_url(path1))
    SVN_ERR(svn_dirent_get_absolute(&abspath1, path1, pool));
    abspath1 = path1;
  SVN_ERR(svn_dirent_get_absolute(&abspath2, path2, pool));
  /* Convert path1 to a URL to feed to do_diff. */
  SVN_ERR(convert_to_url(&url1, ctx->wc_ctx, abspath1, pool, pool));
  SVN_ERR(svn_wc_get_actual_target2(&anchor, &target,
                                    ctx->wc_ctx, path2,
                                    pool, pool));
  /* Fetch the URL of the anchor directory. */
  SVN_ERR(svn_dirent_get_absolute(&anchor_abspath, anchor, pool));
  SVN_ERR(svn_wc__node_get_url(&anchor_url, ctx->wc_ctx, anchor_abspath,
                               pool, pool));
  if (! anchor_url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Directory '%s' has no URL"),
                             svn_dirent_local_style(anchor, pool));
  /* If we are performing a pegged diff, we need to find out what our
     actual URLs will be. */
  if (peg_revision->kind != svn_opt_revision_unspecified)
      svn_opt_revision_t *start_ignore, *end_ignore, end;
      const char *url_ignore;

      end.kind = svn_opt_revision_unspecified;

      SVN_ERR(svn_client__repos_locations(&url1, &start_ignore,
                                          &url_ignore, &end_ignore,
                                          NULL,
                                          path1,
                                          peg_revision,
                                          revision1, &end,
                                          ctx, pool));
      if (!reverse)
          callback_baton->orig_path_1 = url1;
          callback_baton->orig_path_2 =
            svn_path_url_add_component2(anchor_url, target, pool);
          callback_baton->orig_path_1 =
            svn_path_url_add_component2(anchor_url, target, pool);
          callback_baton->orig_path_2 = url1;
  if (use_git_diff_format)
      SVN_ERR(svn_wc__get_wc_root(&callback_baton->wc_root_abspath,
                                  ctx->wc_ctx, anchor_abspath,
                                  pool, pool));
    }
  /* Open an RA session to URL1 to figure out its node kind. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, url1,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));
  /* Resolve the revision to use for URL1. */
  SVN_ERR(svn_client__get_revision_number(&rev, NULL, ctx->wc_ctx,
                                          (strcmp(path1, url1) == 0)
                                                    ? NULL : abspath1,
                                          ra_session, revision1, pool));
  SVN_ERR(svn_ra_check_path(ra_session, "", rev, &kind1, pool));

  /* Figure out the node kind of the local target. */
  SVN_ERR(svn_io_check_resolved_path(abspath2, &kind2, pool));

  callback_baton->ra_session = ra_session;
  callback_baton->anchor = anchor;

  if (!reverse)
    callback_baton->revnum1 = rev;
  else
    callback_baton->revnum2 = rev;
  /* If both diff targets can be diffed as files, fetch the file from the
   * repository and generate a diff against the local version of the file. */
  if ((kind1 == svn_node_file || kind1 == svn_node_none)
       && kind2 == svn_node_file)
    {
      SVN_ERR(diff_repos_wc_file_target(target, abspath2, kind1, rev,
                                        reverse, show_copies_as_adds,
                                        rev2_is_base,
                                        callbacks, callback_baton,
                                        ra_session, ctx, pool));
      return SVN_NO_ERROR;
  /* Else, use the diff editor to generate the diff. */
  SVN_ERR(svn_ra_reparent(ra_session, anchor_url, pool));
                                SVN_RA_CAPABILITY_DEPTH, pool));
  SVN_ERR(svn_wc_get_diff_editor6(&diff_editor, &diff_edit_baton,
                                  show_copies_as_adds,
                                  use_git_diff_format,
                                  callbacks, callback_baton,
                                  pool, pool));
  /* Tell the RA layer we want a delta to change our txn to URL1 */ 
  SVN_ERR(svn_ra_do_diff3(ra_session,
                          &reporter, &reporter_baton,
                          rev,
                          target,
                          diff_depth,
                          ignore_ancestry,
                          TRUE,  /* text_deltas */
                          url1,
                          diff_editor, diff_edit_baton, pool));

  /* Create a txn mirror of path2;  the diff editor will print
     diffs in reverse.  :-)  */
  SVN_ERR(svn_wc_crawl_revisions5(ctx->wc_ctx, abspath2,
                                  reporter, reporter_baton,
                                  FALSE, depth, TRUE, (! server_supports_depth),
                                  FALSE,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  NULL, NULL, /* notification is N/A */
                                  pool));
/* This is basically just the guts of svn_client_diff[_peg]5(). */
do_diff(const svn_wc_diff_callbacks4_t *callbacks,
        struct diff_cmd_baton *callback_baton,
        svn_client_ctx_t *ctx,
        const char *path1,
        const char *path2,
        svn_boolean_t show_copies_as_adds,
        svn_boolean_t use_git_diff_format,
        apr_pool_t *pool)
  SVN_ERR(check_paths(&is_repos1, &is_repos2, path1, path2,
          SVN_ERR(diff_repos_repos(callbacks, callback_baton, ctx,
                                   path1, path2, revision1, revision2,
                                   pool));
      else /* path2 is a working copy path */
          SVN_ERR(diff_repos_wc(path1, revision1, peg_revision,
                                path2, revision2, FALSE, depth,
                                ignore_ancestry, show_copies_as_adds,
                                use_git_diff_format, changelists,
                                callbacks, callback_baton, ctx, pool));
  else /* path1 is a working copy path */
          SVN_ERR(diff_repos_wc(path2, revision2, peg_revision,
                                path1, revision1, TRUE, depth,
                                ignore_ancestry, show_copies_as_adds,
                                use_git_diff_format, changelists,
                                callbacks, callback_baton, ctx, pool));
      else /* path2 is a working copy path */
          SVN_ERR(diff_wc_wc(path1, revision1, path2, revision2,
                             depth, ignore_ancestry, show_copies_as_adds,
                             use_git_diff_format, changelists,
                             callbacks, callback_baton, ctx, pool));

/* Perform a diff summary between two repository paths. */
static svn_error_t *
diff_summarize_repos_repos(svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           const char *path1,
                           const char *path2,
                           const svn_opt_revision_t *revision1,
                           const svn_opt_revision_t *revision2,
                           const svn_opt_revision_t *peg_revision,
                           svn_depth_t depth,
                           svn_boolean_t ignore_ancestry,
                           apr_pool_t *pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;

  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;

  const char *url1;
  const char *url2;
  const char *base_path;
  svn_revnum_t rev1;
  svn_revnum_t rev2;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  const char *anchor1;
  const char *anchor2;
  const char *target1;
  const char *target2;
  svn_ra_session_t *ra_session;

  /* Prepare info for the repos repos diff. */
  SVN_ERR(diff_prepare_repos_repos(&url1, &url2, &base_path, &rev1, &rev2,
                                   &anchor1, &anchor2, &target1, &target2,
                                   &kind1, &kind2, &ra_session,
                                   ctx, path1, path2,
                                   revision1, revision2,
                                   peg_revision, pool));

  if (kind1 == svn_node_none || kind2 == svn_node_none)
    {
      svn_wc_diff_callbacks4_t *callbacks;
      void *callback_baton;

      /* One side of the diff does not exist.
       * Walk the tree that does exist, showing a series of additions
       * or deletions. */
      SVN_ERR(svn_client__get_diff_summarize_callbacks(
                &callbacks, &callback_baton, target1,
                summarize_func, summarize_baton, pool));
      SVN_ERR(diff_repos_repos_added_or_deleted_target(target1, target2,
                                                       rev1, rev2,
                                                       kind1, kind2,
                                                       callbacks,
                                                       callback_baton,
                                                       ra_session,
                                                       pool));
      return SVN_NO_ERROR;
    }

  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used to get the kind of deleted paths.  */
  SVN_ERR(svn_client__open_ra_session_internal(&extra_ra_session, NULL,
                                               anchor1, NULL, NULL, FALSE,
                                               TRUE, ctx, pool));

  /* Set up the repos_diff editor. */
  SVN_ERR(svn_client__get_diff_summarize_editor
          (target2, summarize_func,
           summarize_baton, extra_ra_session, rev1, ctx->cancel_func,
           ctx->cancel_baton, &diff_editor, &diff_edit_baton, pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3
          (ra_session, &reporter, &reporter_baton, rev2, target1,
           depth, ignore_ancestry,
           FALSE /* do not create text delta */, url2, diff_editor,
           diff_edit_baton, pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL, pool));
  return reporter->finish_report(reporter_baton, pool);
}

/* This is basically just the guts of svn_client_diff_summarize[_peg]2(). */
static svn_error_t *
do_diff_summarize(svn_client_diff_summarize_func_t summarize_func,
                  void *summarize_baton,
                  svn_client_ctx_t *ctx,
                  const char *path1,
                  const char *path2,
                  const svn_opt_revision_t *revision1,
                  const svn_opt_revision_t *revision2,
                  const svn_opt_revision_t *peg_revision,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  apr_pool_t *pool)
{
  svn_boolean_t is_repos1;
  svn_boolean_t is_repos2;

  /* Check if paths/revisions are urls/local. */
  SVN_ERR(check_paths(&is_repos1, &is_repos2, path1, path2,
                      revision1, revision2, peg_revision));

  if (is_repos1 && is_repos2)
    return diff_summarize_repos_repos(summarize_func, summarize_baton, ctx,
                                      path1, path2, revision1, revision2,
                                      peg_revision, depth, ignore_ancestry,
                                      pool);
  else
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                            _("Summarizing diff can only compare repository "
                              "to repository"));
}


/* Initialize DIFF_CMD_BATON.diff_cmd and DIFF_CMD_BATON.options,
 * Allocate the fields in POOL, which should be at least as long-lived
 * as the pool DIFF_CMD_BATON itself is allocated in.
set_up_diff_cmd_and_options(struct diff_cmd_baton *diff_cmd_baton,
                            const apr_array_header_t *options,
                            apr_hash_t *config, apr_pool_t *pool)
      svn_config_t *cfg = apr_hash_get(config, SVN_CONFIG_CATEGORY_CONFIG,
                                       APR_HASH_KEY_STRING);
            options = svn_cstring_split(diff_extensions, " \t\n\r", TRUE, pool);
    options = apr_array_make(pool, 0, sizeof(const char *));
    SVN_ERR(svn_path_cstring_to_utf8(&diff_cmd_baton->diff_cmd, diff_cmd,
                                     pool));
    diff_cmd_baton->diff_cmd = NULL;
  if (diff_cmd_baton->diff_cmd)
          argv = apr_palloc(pool, argc * sizeof(char *));
                      APR_ARRAY_IDX(options, i, const char *), pool));
      diff_cmd_baton->options.for_external.argv = argv;
      diff_cmd_baton->options.for_external.argc = argc;
      diff_cmd_baton->options.for_internal
        = svn_diff_file_options_create(pool);
      SVN_ERR(svn_diff_file_options_parse
              (diff_cmd_baton->options.for_internal, options, pool));
svn_client_diff5(const apr_array_header_t *options,
                 const char *path1,
                 const char *path2,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
  struct diff_cmd_baton diff_cmd_baton = { 0 };
  svn_wc_diff_callbacks4_t diff_callbacks;
  svn_opt_revision_t peg_revision;
  diff_callbacks.file_opened = diff_file_opened;
  diff_callbacks.file_changed = diff_file_changed;
  diff_callbacks.file_added = diff_file_added;
  diff_callbacks.file_deleted = no_diff_deleted ? diff_file_deleted_no_diff :
                                                  diff_file_deleted_with_diff;
  diff_callbacks.dir_added =  diff_dir_added;
  diff_callbacks.dir_deleted = diff_dir_deleted;
  diff_callbacks.dir_props_changed = diff_dir_props_changed;
  diff_callbacks.dir_opened = diff_dir_opened;
  diff_callbacks.dir_closed = diff_dir_closed;

  diff_cmd_baton.orig_path_1 = path1;
  diff_cmd_baton.orig_path_2 = path2;

  SVN_ERR(set_up_diff_cmd_and_options(&diff_cmd_baton, options,
                                      ctx->config, pool));
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;
  diff_cmd_baton.relative_to_dir = relative_to_dir;
  diff_cmd_baton.use_git_diff_format = use_git_diff_format;
  diff_cmd_baton.wc_ctx = ctx->wc_ctx;
  diff_cmd_baton.visited_paths = apr_hash_make(pool);
  diff_cmd_baton.ra_session = NULL;
  diff_cmd_baton.wc_root_abspath = NULL;
  diff_cmd_baton.anchor = NULL;

  return do_diff(&diff_callbacks, &diff_cmd_baton, ctx,
                 path1, path2, revision1, revision2, &peg_revision,
                 depth, ignore_ancestry, show_copies_as_adds,
                 use_git_diff_format, changelists, pool);
svn_client_diff_peg5(const apr_array_header_t *options,
                     const char *path,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
  struct diff_cmd_baton diff_cmd_baton = { 0 };
  svn_wc_diff_callbacks4_t diff_callbacks;
  diff_callbacks.file_opened = diff_file_opened;
  diff_callbacks.file_changed = diff_file_changed;
  diff_callbacks.file_added = diff_file_added;
  diff_callbacks.file_deleted = no_diff_deleted ? diff_file_deleted_no_diff :
                                                  diff_file_deleted_with_diff;
  diff_callbacks.dir_added =  diff_dir_added;
  diff_callbacks.dir_deleted = diff_dir_deleted;
  diff_callbacks.dir_props_changed = diff_dir_props_changed;
  diff_callbacks.dir_opened = diff_dir_opened;
  diff_callbacks.dir_closed = diff_dir_closed;

  diff_cmd_baton.orig_path_1 = path;
  diff_cmd_baton.orig_path_2 = path;

  SVN_ERR(set_up_diff_cmd_and_options(&diff_cmd_baton, options,
                                      ctx->config, pool));
  diff_cmd_baton.pool = pool;
  diff_cmd_baton.outfile = outfile;
  diff_cmd_baton.errfile = errfile;
  diff_cmd_baton.header_encoding = header_encoding;
  diff_cmd_baton.revnum1 = SVN_INVALID_REVNUM;
  diff_cmd_baton.revnum2 = SVN_INVALID_REVNUM;

  diff_cmd_baton.force_empty = FALSE;
  diff_cmd_baton.force_binary = ignore_content_type;
  diff_cmd_baton.relative_to_dir = relative_to_dir;
  diff_cmd_baton.use_git_diff_format = use_git_diff_format;
  diff_cmd_baton.wc_ctx = ctx->wc_ctx;
  diff_cmd_baton.visited_paths = apr_hash_make(pool);
  diff_cmd_baton.ra_session = NULL;
  diff_cmd_baton.wc_root_abspath = NULL;
  diff_cmd_baton.anchor = NULL;

  return do_diff(&diff_callbacks, &diff_cmd_baton, ctx,
                 path, path, start_revision, end_revision, peg_revision,
                 depth, ignore_ancestry, show_copies_as_adds,
                 use_git_diff_format, changelists, pool);
svn_client_diff_summarize2(const char *path1,
                           const char *path2,
  svn_opt_revision_t peg_revision;
  /* ### CHANGELISTS parameter isn't used */
  return do_diff_summarize(summarize_func, summarize_baton, ctx,
                           path1, path2, revision1, revision2, &peg_revision,
                           depth, ignore_ancestry, pool);
svn_client_diff_summarize_peg2(const char *path,
  /* ### CHANGELISTS parameter isn't used */
  return do_diff_summarize(summarize_func, summarize_baton, ctx,
                           path, path, start_revision, end_revision,
                           peg_revision, depth, ignore_ancestry, pool);
svn_client_diff_summarize_t *
svn_client_diff_summarize_dup(const svn_client_diff_summarize_t *diff,
                              apr_pool_t *pool)
{
  svn_client_diff_summarize_t *dup_diff = apr_palloc(pool, sizeof(*dup_diff));

  *dup_diff = *diff;

  if (diff->path)
    dup_diff->path = apr_pstrdup(pool, diff->path);

  return dup_diff;
}