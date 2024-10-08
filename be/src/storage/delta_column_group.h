// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "common/status.h"
#include "common/statusor.h"
#include "fmt/format.h"
#include "gen_cpp/lake_types.pb.h"
#include "gen_cpp/olap_common.pb.h"
#include "storage/olap_common.h"

namespace starrocks {

// `DeltaColumnGroup` is used for record the new update columns data
// It is generated when column mode partial update happen.
// `_column_ids` record the columns which have been updated, and `_column_file` points to the data file
// which contains update columns.
// Column data file end as `.cols`.
class DeltaColumnGroup;
using DeltaColumnGroupPtr = std::shared_ptr<DeltaColumnGroup>;
using DeltaColumnGroupList = std::vector<DeltaColumnGroupPtr>;

class DeltaColumnGroup {
public:
    DeltaColumnGroup() {}
    ~DeltaColumnGroup() {}
    void init(int64_t version, const std::vector<std::vector<ColumnUID>>& column_ids,
              const std::vector<std::string>& column_files, const std::vector<std::string>& encryption_metas = {});
    Status load(int64_t version, const char* data, size_t length);
    Status load(int64_t version, const DeltaColumnGroupVerPB& dcg_ver_pb);
    std::string save() const;
    // merge this dcg into dst dcgs by version, returns the number of successful merges
    int merge_into_by_version(DeltaColumnGroupList& dcgs, const std::string& dir, const RowsetId& rowset_id,
                              int segment_id);
    // merge src dcg into this dcg by version and change the src dcg's file name suffix
    bool merge_by_version(DeltaColumnGroup& dcg, const std::string& dir, const RowsetId& rowset_id, int segment_id);

    std::pair<int32_t, int32_t> get_column_idx(ColumnUID uid) const {
        for (int idx = 0; idx < _column_uids.size(); ++idx) {
            for (int cidx = 0; cidx < _column_uids[idx].size(); cidx++) {
                // it is impossible that multiple _column_ids[idx][cidx]
                // will hit cid in a single dcg.
                if (_column_uids[idx][cidx] == uid) {
                    return std::pair<int32_t, int32_t>{std::pair{idx, cidx}};
                }
            }
        }
        return std::pair<int32_t, int32_t>{std::pair{-1, -1}};
    }

    // `_column_file` contains `$1_$2_$3_$4.cols`, $1 is rowsetid, $2 is segment id. $3 is version . $4 is seq suffix
    std::vector<std::string> column_files(const std::string& dir_path) const {
        std::vector<std::string> column_files = _column_files;
        for (auto& column_file : column_files) {
            column_file = dir_path + "/" + column_file;
        }
        return column_files;
    }

    StatusOr<std::string> column_file_by_idx(const std::string& dir_path, uint32_t idx) const {
        if (idx >= _column_files.size()) {
            return Status::InvalidArgument(fmt::format("column_file_by_idx fail, path: {} column file cnt: {} idx: {}",
                                                       dir_path, _column_files.size(), idx));
        }
        return dir_path + "/" + _column_files[idx];
    }

    // TODO: rename
    std::vector<std::vector<ColumnUID>>& column_ids() { return _column_uids; }
    // TODO: rename
    const std::vector<std::vector<ColumnUID>>& column_ids() const { return _column_uids; }
    int64_t version() const { return _version; }

    std::string debug_string() {
        std::stringstream ss;
        ss << "ver:" << _version << ", ";
        for (int i = 0; i < _column_files.size(); ++i) {
            ss << "file:" << _column_files[i] << ", ";
            ss << "uids:";
            for (auto uid : _column_uids[i]) {
                ss << uid << "|";
            }

            ss << "\n";
        }
        return ss.str();
    }

    size_t memory_usage() const { return _memory_usage; }

    const std::vector<std::string>& relative_column_files() const { return _column_files; }

    const std::vector<std::string>& encryption_metas() const { return _encryption_metas; }

private:
    void _calc_memory_usage();

private:
    int64_t _version = 0;
    std::vector<std::vector<ColumnUID>> _column_uids;
    std::vector<std::string> _column_files;
    std::vector<std::string> _encryption_metas;
    size_t _memory_usage = 0;
};

class DeltaColumnGroupLoader {
public:
    DeltaColumnGroupLoader() = default;
    virtual ~DeltaColumnGroupLoader() = default;
    // Used for PK table
    virtual Status load(const TabletSegmentId& tsid, int64_t version, DeltaColumnGroupList* pdcgs) = 0;
    // Used for non-PK table
    virtual Status load(int64_t tablet_id, RowsetId rowsetid, uint32_t segment_id, int64_t version,
                        DeltaColumnGroupList* pdcgs) = 0;
};

class DeltaColumnGroupListSerializer {
public:
    static std::string serialize_delta_column_group_list(const DeltaColumnGroupList& dcgs,
                                                         DeltaColumnGroupListPB* dst = nullptr);
    static Status deserialize_delta_column_group_list(const char* data, size_t length, DeltaColumnGroupList* dcgs);
    static Status deserialize_delta_column_group_list(const DeltaColumnGroupListPB& dcgs_pb,
                                                      DeltaColumnGroupList* dcgs);
    static Status _deserialize_delta_column_group_list(const DeltaColumnGroupListPB& dcgs_pb,
                                                       DeltaColumnGroupList* dcgs);
};

class DeltaColumnGroupListHelper {
public:
    static void garbage_collection(DeltaColumnGroupList& dcg_list, const TabletSegmentId& tsid,
                                   int64_t min_readable_version, const std::string& tablet_path,
                                   std::vector<std::pair<TabletSegmentId, int64_t>>* garbage_dcgs,
                                   std::vector<std::string>* garbage_files);

    // used for non-Primary Key tablet only
    static Status save_snapshot(const std::string& file_path, DeltaColumnGroupSnapshotPB& dcg_snapshot_pb);
    static Status parse_snapshot(const std::string& file_path, DeltaColumnGroupSnapshotPB& dcg_snapshot_pb);
};

} // namespace starrocks