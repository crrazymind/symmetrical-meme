/* wc-test-queries.sql -- queries used to verify wc metadata from
 *                        the C tests.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

-- STMT_SELECT_NODES_INFO
SELECT op_depth, nodes.presence, nodes.local_relpath, revision,
       repos_path, file_external, def_local_relpath, moved_to, moved_here,
       properties
FROM nodes
LEFT OUTER JOIN externals
            ON nodes.wc_id = externals.wc_id
                AND nodes.local_relpath = externals.local_relpath
WHERE nodes.local_relpath = ?1 OR nodes.local_relpath LIKE ?2

-- STMT_SELECT_ACTUAL_INFO
SELECT local_relpath
FROM actual_node
WHERE conflict_data is NOT NULL AND local_relpath = ?1 OR local_relpath LIKE ?2

-- STMT_DELETE_NODES
DELETE FROM nodes;

-- STMT_INSERT_NODE
INSERT INTO nodes (local_relpath, op_depth, presence, repos_path,
                   revision, parent_relpath, wc_id, repos_id, kind, depth)
           VALUES (?1, ?2, ?3, ?4, ?5, ?6, 1, 1, 'dir', 'infinity')

-- STMT_DELETE_ACTUAL
DELETE FROM actual_node;

-- STMT_INSERT_ACTUAL
INSERT INTO actual_node (local_relpath, parent_relpath, changelist, wc_id)
                VALUES (?1, ?2, ?3, 1)

-- STMT_NODES_SET_FILE
UPDATE nodes SET kind = 'file' WHERE wc_id = 1 and local_relpath = ?1

-- STMT_SELECT_ALL_ACTUAL
SELECT local_relpath FROM actual_node WHERE wc_id = 1

