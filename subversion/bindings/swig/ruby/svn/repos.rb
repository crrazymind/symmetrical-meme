require "English"
require "svn/error"
require "svn/util"
require "svn/core"
require "svn/fs"
require "svn/ext/repos"

module Svn
  module Repos
    Util.set_constants(Ext::Repos, self)
    Util.set_methods(Ext::Repos, self)


    @@alias_targets = %w(create open hotcopy recover
                         db_logfiles)
    class << self
      @@alias_targets.each do |target|
        alias_method "_#{target}", target
      end
    end
    @@alias_targets.each do |target|
      alias_method "_#{target}", target
    end
    @@alias_targets = nil
    
    module_function
    def open(path)
      repos = _open(path)
      if block_given?
        yield repos
      else
        repos
      end
    end

    def create(path, config={}, fs_config={})
      _create(path, nil, nil, config, fs_config)
    end

    def hotcopy(src, dest, clean_logs=true)
      _hotcopy(src, dest, clean_logs)
    end

    def recover(path,	nonblocking=false)
      start_callback = Proc.new do
        yield
      end
      recover2(path, nonblocking, start_callback)
    end

    def db_logfiles(path, only_unused=true)
      _db_logfiles(path, only_unused)
    end

    def read_authz(file, must_exist=true)
      Repos.authz_read(file, must_exist)
    end
      
    ReposCore = SWIG::TYPE_p_svn_repos_t
    class ReposCore
      class << self
        def def_simple_delegate(*ids)
          ids.each do |id|
            module_eval(<<-EOC, __FILE__, __LINE__)
            def #{id.to_s}
              Repos.#{id.to_s}(self)
            end
            EOC
          end
        end
      end

      def_simple_delegate :path, :db_env, :conf_dir
      def_simple_delegate :svnserve_conf, :lock_dir
      def_simple_delegate :db_lockfile, :db_logs_lockfile
      def_simple_delegate :hook_dir, :start_commit_hook
      def_simple_delegate :pre_commit_hook, :post_commit_hook
      def_simple_delegate :pre_revprop_change_hook, :post_revprop_change_hook
      def_simple_delegate :pre_lock_hook, :post_lock_hook
      def_simple_delegate :pre_unlock_hook, :post_unlock_hook

      attr_reader :authz_read_func

      def fs
        Repos.fs_wrapper(self)
      end
      
      def set_authz_read_func(&block)
        @authz_read_func = block
      end
      
      def report(rev, username, fs_base, target, tgt_path,
                 editor, text_deltas=true, recurse=true,
                 ignore_ancestry=false, authz_read_func=nil)
        authz_read_func ||= @authz_read_func
        args = [
          rev, username, self, fs_base, target, tgt_path,
          text_deltas, recurse, ignore_ancestry, editor,
          authz_read_func,
        ]
        report_baton = Repos.begin_report(*args)
        setup_report_baton(report_baton)
        if block_given?
          report_baton.set_path("", rev)
          result = yield(report_baton)
          report_baton.finish_report unless report_baton.aborted?
          result
        else
          report_baton
        end
      end

      def commit_editor(repos_url, base_path, txn=nil, user=nil,
                        log_msg=nil, commit_callback=nil,
                        authz_callback=nil)
        editor, baton = Repos.get_commit_editor3(self, txn, repos_url,
                                                 base_path, user, log_msg,
                                                 commit_callback,
                                                 authz_callback)
        editor.baton = baton
        editor
      end
      
      def youngest_rev
        fs.youngest_rev
      end

      def dated_revision(date)
        Repos.dated_revision(self, date.to_apr_time)
      end

      def logs(paths, start_rev, end_rev, limit,
               discover_changed_paths=true,
               strict_node_history=false,
               authz_read_func=nil)
        authz_read_func ||= @authz_read_func
        paths = [paths] unless paths.is_a?(Array)
        infos = []
        receiver = Proc.new do |changed_paths, revision, author, date, message|
          date = Time.parse_svn_format(date) if date
          if block_given?
            yield(changed_paths, revision, author, date, message)
          end
          infos << [changed_paths, revision, author, date, message]
        end
        Repos.get_logs3(self, paths, start_rev, end_rev,
                        limit, discover_changed_paths,
                        strict_node_history, authz_read_func,
                        receiver)
        infos
      end

      def file_revs(path, start_rev, end_rev, authz_read_func=nil)
        authz_read_func ||= @authz_read_func
        revs = []
        handler = Proc.new do |path, rev, rev_props, prop_diffs|
          yield(path, rev, rev_props, prop_diffs) if block_given?
          revs << [path, rev, rev_props, prop_diffs]
        end
        Repos.get_file_revs(self, path, start_rev, end_rev,
                            authz_read_func, handler)
        revs
      end

      def commit_txn(txn)
        Repos.fs_commit_txn(self, txn)
      end

      def transaction_for_commit(author, log, rev=nil)
        txn = nil
        args = [self, rev || youngest_rev, author, log]
        txn = Repos.fs_begin_txn_for_commit(*args)
        
        if block_given?
          yield(txn)
          commit(txn) if fs.transactions.include?(txn.name)
        else
          txn
        end
      end

      def transaction_for_update(author, rev=nil)
        txn = nil
        args = [self, rev || youngest_rev, author]
        txn = Repos.fs_begin_txn_for_update(*args)
        
        if block_given?
          yield(txn)
          txn.abort if fs.transactions.include?(txn.name)
        else
          txn
        end
      end
      
      def commit(txn)
        Repos.fs_commit_txn(self, txn)
      end

      def lock(path, token=nil, comment=nil, dav_comment=true,
               expiration_date=nil, current_rev=nil, steal_lock=false)
        if expiration_date
          expiration_date = expiration_date.to_apr_time
        else
          expiration_date = 0
        end
        current_rev ||= youngest_rev
        Repos.fs_lock(self, path, token, comment,
                      dav_comment, expiration_date,
                      current_rev, steal_lock)
      end

      def unlock(path, token, break_lock=false)
        Repos.fs_unlock(self, path, token, break_lock)
      end

      def get_locks(path, authz_read_func=nil)
        authz_read_func ||= @authz_read_func
        Repos.fs_get_locks(self, path, authz_read_func)
      end

      def set_prop(author, name, new_value, rev=nil, authz_read_func=nil)
        authz_read_func ||= @authz_read_func
        rev ||= youngest_rev
        Repos.fs_change_rev_prop2(self, rev, author, name,
                                  new_value, authz_read_func)
      end

      def prop(name, rev=nil, authz_read_func=nil)
        authz_read_func ||= @authz_read_func
        rev ||= youngest_rev
        Repos.fs_revision_prop(self, rev, name, authz_read_func)
      end

      def proplist(rev=nil, authz_read_func=nil)
        authz_read_func ||= @authz_read_func
        rev ||= youngest_rev
        Repos.fs_revision_proplist(self, rev, authz_read_func)
      end

      def node_editor(base_root, root)
        editor, baton = Repos.node_editor(self, base_root, root)
        def baton.node
          Repos.node_from_baton(self)
        end
        editor.baton = baton
        editor
      end

      def dump_fs(dumpstream, feedback_stream, start_rev, end_rev,
                  incremental=true, use_deltas=true, &cancel_func)
        Repos.dump_fs2(self, dumpstream, feedback_stream,
                       start_rev, end_rev, incremental,
                       use_deltas, cancel_func)
      end

      def load_fs(dumpstream, feedback_stream, uuid_action,
                  parent_dir, use_pre_commit_hook=true,
                  use_post_commit_hook=true, &cancel_func)
        Repos.load_fs2(self, dumpstream, feedback_stream,
                       uuid_action, parent_dir,
                       use_pre_commit_hook, use_post_commit_hook,
                       cancel_func)
      end

      def build_parser(uuid_action, parent_dir,
                       use_history=true, outstream=nil)
        outstream ||= StringIO.new
        parser, baton = Repos.get_fs_build_parser2(use_history,
                                                   uuid_action,
                                                   outstream,
                                                   parent_dir)
        def parser.parse_dumpstream(stream, &cancel_func)
          Repos.parse_dumpstream2(stream, self, @baton, cancel_func)
        end

        def parser.outstream=(new_stream)
          @outstream = new_stream
        end
      
        def parser.baton=(new_baton)
          @baton = new_baton
        end
        
        def parser.baton
          @baton
        end

        parser.outstream = outstream
        parser.baton = baton
        parser
      end
    
      def delta_tree(root, base_rev)
        base_root = fs.root(base_rev)
        editor = node_editor(base_root, root)
        root.replay(editor)
        editor.baton.node
      end
      
      private
      def setup_report_baton(baton)
        baton.instance_variable_set("@aborted", false)
        
        def baton.aborted?
          @aborted
        end
        
        def baton.set_path(path, revision, start_empty=false, lock_token=nil)
          Repos.set_path2(self, path, revision, start_empty, lock_token)
        end
        
        def baton.link_path(path, link_path, revision,
                            start_empty=false, lock_token=nil)
          Repos.link_path2(self, path, link_path, revision,
                           start_empty, lock_token)
        end
        
        def baton.delete_path(path)
          Repos.delete_path(self, path)
        end
        
        def baton.finish_report
          Repos.finish_report(self)
        end
        
        def baton.abort_report
          Repos.abort_report(self)
          @aborted = true
        end
        
      end
    end
    
    
    class Node
      
      alias text_mod? text_mod
      alias prop_mod? prop_mod
      
      def copy?
        Util.copy?(copyfrom_path, copyfrom_rev)
      end
      
      def add?
        action == "A"
      end
      
      def delete?
        action == "D"
      end

      def replace?
        action == "R"
      end

      def file?
        kind == Core::NODE_FILE
      end

      def dir?
        kind == Core::NODE_DIR
      end

      def none?
        kind == Core::NODE_NONE
      end

      def unknown?
        kind == Core::NODE_UNKNOWN
      end
      
    end

    Authz = SWIG::TYPE_p_svn_authz_t
    class Authz

      class << self
        def read(file, must_exist=true)
          Repos.authz_read(file, must_exist)
        end
      end
      
      def can_access?(repos_name, path, user, required_access)
        Repos.authz_check_access(self,
                                 repos_name,
                                 path,
                                 user,
                                 required_access)
      end
    end
  end
end
