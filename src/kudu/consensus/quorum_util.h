// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef KUDU_CONSENSUS_QUORUM_UTIL_H_
#define KUDU_CONSENSUS_QUORUM_UTIL_H_

#include <string>

#include "kudu/consensus/metadata.pb.h"
#include "kudu/util/status.h"

namespace kudu {
namespace consensus {

enum RaftConfigState {
  PENDING_CONFIG,
  COMMITTED_CONFIG,
  ACTIVE_CONFIG,
};

// Policy for attempted Raft configuration change when replacing replicas
// (i.e. options for the Should{Add,Evict}Replica() functions).
enum class MajorityHealthPolicy {
  // While trying to replace a replica, attempt to change the Raft configuration
  // only if the majority of voter replicas is reported as on-line/healthy
  // (this applies to the resulting configuration).
  HONOR,

  // While trying to replace a replica, attempt to change the Raft configuration
  // even if the majority of voter replicas is not reported as on-line/healthy.
  IGNORE,
};

bool IsRaftConfigMember(const std::string& uuid, const RaftConfigPB& config);
bool IsRaftConfigVoter(const std::string& uuid, const RaftConfigPB& config);
bool GetRaftConfigMemberRegion(const std::string& uuid, const RaftConfigPB& config,
    bool *is_voter, std::string *region);
bool GetRaftConfigMemberQuorumId(const std::string& uuid, const RaftConfigPB& config,
    bool *is_voter, std::string *quorum_id);
bool IsRaftConfigMemberWithDetail(const std::string& uuid,
    const RaftConfigPB& config, std::string *hostname_port,
    bool *is_voter, std::string *region);

// Whether the specified Raft role is attributed to a peer which can participate
// in leader elections.
bool IsVoterRole(RaftPeerPB::Role role);

// Get the specified member of the config.
// Returns Status::NotFound if a member with the specified uuid could not be
// found in the config.
Status GetRaftConfigMember(RaftConfigPB* config,
                           const std::string& uuid,
                           RaftPeerPB** peer_pb);

// Get the leader of the consensus configuration.
// Returns Status::NotFound() if the leader RaftPeerPB could not be found in
// the config, or if there is no leader defined.
Status GetRaftConfigLeader(ConsensusStatePB* cstate, RaftPeerPB** peer_pb);

// Modifies 'configuration' remove the peer with the specified 'uuid'.
// Returns false if the server with 'uuid' is not found in the configuration.
// Returns true on success.
bool RemoveFromRaftConfig(RaftConfigPB* config, const std::string& uuid);

// Returns true iff the two peers have equivalent replica types and associated
// options.
bool ReplicaTypesEqual(const RaftPeerPB& peer1, const RaftPeerPB& peer2);

// Counts the number of voters in the configuration.
int CountVoters(const RaftConfigPB& config);

// Calculates size of a configuration majority based on # of voters.
int MajoritySize(int num_voters);

// Based on `commit_req`, this helper computes the commit requirement
// (number of votes) required from the total number of voters passed in as an
// argument.
int ResolveCommitRequirement(int total_voters, const std::string& commit_req);

// Parses a string representation of quorum requirement and returns an integer.
// -1 is returned if `commit_req` represents "majority".
int ParseCommitRequirement(const std::string& commit_req);

// Determines the role that the peer with uuid 'peer_uuid' plays in the
// cluster. If 'peer_uuid' is empty or is not a member of the configuration,
// this function will return NON_PARTICIPANT, regardless of whether it is
// specified as the leader in 'leader_uuid'. Likewise, if 'peer_uuid' is a
// NON_VOTER in the config, this function will return LEARNER, regardless of
// whether it is specified as the leader in 'leader_uuid' (although that
// situation is illegal in practice).
RaftPeerPB::Role GetConsensusRole(const std::string& peer_uuid,
                                  const std::string& leader_uuid,
                                  const RaftConfigPB& config);

// Same as above, but uses the leader and active role from the given
// ConsensusStatePB.
RaftPeerPB::Role GetConsensusRole(const std::string& peer_uuid,
                                  const ConsensusStatePB& cstate);

// Verifies that the provided configuration is well formed.
Status VerifyRaftConfig(const RaftConfigPB& config);

// Superset of checks performed by VerifyRaftConfig. Also ensures that the
// leader is a configuration voter, if it is set, and that a valid term is set.
Status VerifyConsensusState(const ConsensusStatePB& cstate);

// Provide a textual description of the difference between two consensus states,
// suitable for logging.
std::string DiffConsensusStates(const ConsensusStatePB& old_state,
                                const ConsensusStatePB& new_state,
                                std::vector<std::string>* evicted_peers = nullptr);

// Same as the above, but just the RaftConfigPB portion of the configuration.
// If some peers are evicted in the new_config, then returns the evicted peer
// uuids in 'evicted_peers'
std::string DiffRaftConfigs(const RaftConfigPB& old_config,
                            const RaftConfigPB& new_config,
                            std::vector<std::string>* evicted_peers = nullptr);

// Return 'true' iff the specified tablet configuration is under-replicated
// given the 'replication_factor' and should add a replica. The decision is
// based on the health information provided by the Raft configuration
// in the 'config' parameter and the policy specified by the 'policy' parameter.
bool ShouldAddReplica(const RaftConfigPB& config,
                      int replication_factor,
                      MajorityHealthPolicy policy);

// Check if the given Raft configuration contains at least one extra replica
// which should (and can) be removed in accordance with the specified
// replication factor, current Raft leader, and the given policy. If so,
// then return 'true' and set the UUID of the best candidate for eviction
// into the 'uuid_to_evict' out parameter. Otherwise, return 'false'.
bool ShouldEvictReplica(const RaftConfigPB& config,
                        const std::string& leader_uuid,
                        int replication_factor,
                        MajorityHealthPolicy policy,
                        std::string* uuid_to_evict = nullptr);

// Helper function to compute the actual voter count from the config. If the
// leader_uuid is present, it also figures out the quorum_id of the leader.
void GetActualVoterCountsFromConfig(
    const RaftConfigPB& config, const std::string& leader_uuid,
    std::map<std::string, int>* actual_voter_counts,
    std::string* leader_quorum_id = nullptr,
    bool backed_by_db_only = false);

// Make each regions voter count, the max of voter distribution and current
// voters
void AdjustVoterDistributionWithCurrentVoters(
    const RaftConfigPB& config,
    std::map<std::string, int> *voter_distribution);

// For QuorumType = Region, it returns the current VD. For QuorumType = QuorumID,
// it returns default quorum size with current voter quorums, and overrided by
// current VD.
void GetVoterDistributionForQuorumId(
    const RaftConfigPB& config,
    std::map<std::string, int>* quorum_id_vd
);

// Is this mode a static quorum mode type?
bool IsStaticQuorumMode(QuorumMode mode);

// Use quorum_id instead of region for flexiraft?
bool IsUseQuorumId(const CommitRulePB& commit_rule);

// Return quorum_id or region based on current commit rule's QuorumType
std::string GetQuorumId(const RaftPeerPB& peer, const CommitRulePB& commit_rule);

// Return quorum_id or region based on whether use quorum_id
std::string GetQuorumId(const RaftPeerPB& peer, bool use_quorum_id);

// Return true of the peer has a non-empty quorum_id
bool PeerHasValidQuorumId(const RaftPeerPB& peer);

}  // namespace consensus
}  // namespace kudu

#endif /* KUDU_CONSENSUS_QUORUM_UTIL_H_ */
