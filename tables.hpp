#pragma once


struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] config {
  eosio::asset                      minimum_stake_amount;
  eosio::asset                      minimum_unliquify_amount;
  uint64_t                          seconds_between_distributions;
  uint64_t                          max_snapshots_to_process;
  uint64_t                          initial_epoch_start_time;
  //uint64_t                          swax_epoch_length_seconds;
  uint64_t                          cpu_rental_epoch_length_seconds;
  uint64_t                          seconds_between_epochs; /* epochs overlap, this is 1 week */
  double                            user_share;
  double                            pol_share;
  std::vector<revenue_receiver>     ecosystem_fund;
  std::vector<eosio::name>          admin_wallets;
  std::vector<eosio::name>          cpu_contracts;
  uint64_t                          redemption_period_length_seconds;
  uint64_t                          seconds_between_stakeall;
  eosio::name                       fallback_cpu_receiver;

  EOSLIB_SERIALIZE(config, (minimum_stake_amount)
                            (minimum_unliquify_amount)
                            (seconds_between_distributions)
                            (max_snapshots_to_process)
                            (initial_epoch_start_time)
                            (cpu_rental_epoch_length_seconds)
                            (seconds_between_epochs)
                            (user_share)
                            (pol_share)
                            (ecosystem_fund)
                            (admin_wallets)
                            (cpu_contracts)
                            (redemption_period_length_seconds)
                            (seconds_between_stakeall)
                            (fallback_cpu_receiver)
                            )
};
using config_singleton = eosio::singleton<"config"_n, config>;


struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] debug {
  uint64_t      ID;
  std::string   message;
  
  uint64_t primary_key() const { return ID; }
};
using debug_table = eosio::multi_index<"debug"_n, debug
>;


struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] ecosystem {
  eosio::name                   beneficiary;
  eosio::asset                  wax_balance;
  eosio::asset                  total_wax_received;
  
  uint64_t primary_key() const { return beneficiary.value; }
};
using eco_table = eosio::multi_index<"ecosystem"_n, ecosystem
>;


struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] epochs {
  uint64_t          start_time;
  uint64_t          time_to_unstake;
  eosio::name       cpu_wallet;
  eosio::asset      wax_bucket;
  eosio::asset      wax_to_refund;
  uint64_t          redemption_period_start_time;
  uint64_t          redemption_period_end_time;
  eosio::asset      total_cpu_funds_returned;
  eosio::asset      total_added_to_redemption_bucket;
  
  uint64_t primary_key() const { return start_time; }
};
using epochs_table = eosio::multi_index<"epochs"_n, epochs
>;

inline eosio::block_signing_authority convert_to_block_signing_authority( const eosio::public_key& producer_key ) {
  return eosio::block_signing_authority_v0{ .threshold = 1, .keys = {{producer_key, 1}} };
} 

struct [[eosio::table]] producer_info {
  eosio::name                                              owner;
  double                                                   total_votes = 0;
  eosio::public_key                                        producer_key; /// a packed public key object
  bool                                                     is_active = true;
  std::string                                              url;
  uint32_t                                                 unpaid_blocks = 0;
  eosio::time_point                                        last_claim_time;
  uint16_t                                                 location = 0;
  eosio::binary_extension<eosio::block_signing_authority>  producer_authority; // added in version 1.9.0

  uint64_t primary_key()const { return owner.value;                             }
  double   by_votes()const    { return is_active ? -total_votes : total_votes;  }
  bool     active()const      { return is_active;                               }
  void     deactivate()       { producer_key = eosio::public_key(); producer_authority.reset(); is_active = false; }

  eosio::block_signing_authority get_producer_authority()const {
     if( producer_authority.has_value() ) {
        bool zero_threshold = std::visit( [](auto&& auth ) -> bool {
           return (auth.threshold == 0);
        }, *producer_authority );
        // zero_threshold could be true despite the validation done in regproducer2 because the v1.9.0 eosio.system
        // contract has a bug which may have modified the producer table such that the producer_authority field
        // contains a default constructed eosio::block_signing_authority (which has a 0 threshold and so is invalid).
        if( !zero_threshold ) return *producer_authority;
     }
     return convert_to_block_signing_authority( producer_key );
  }

  // The unregprod and claimrewards actions modify unrelated fields of the producers table and under the default
  // serialization behavior they would increase the size of the serialized table if the producer_authority field
  // was not already present. This is acceptable (though not necessarily desired) because those two actions require
  // the authority of the producer who pays for the table rows.
  // However, the rmvproducer action and the onblock transaction would also modify the producer table in a similar
  // way and increasing its serialized size is not acceptable in that context.
  // So, a custom serialization is defined to handle the binary_extension producer_authority
  // field in the desired way. (Note: v1.9.0 did not have this custom serialization behavior.)

  template<typename DataStream>
  friend DataStream& operator << ( DataStream& ds, const producer_info& t ) {
     ds << t.owner
        << t.total_votes
        << t.producer_key
        << t.is_active
        << t.url
        << t.unpaid_blocks
        << t.last_claim_time
        << t.location;

     if( !t.producer_authority.has_value() ) return ds;

     return ds << t.producer_authority;
  }

  template<typename DataStream>
  friend DataStream& operator >> ( DataStream& ds, producer_info& t ) {
     return ds >> t.owner
               >> t.total_votes
               >> t.producer_key
               >> t.is_active
               >> t.url
               >> t.unpaid_blocks
               >> t.last_claim_time
               >> t.location
               >> t.producer_authority;
  }
};
typedef eosio::multi_index< "producers"_n, producer_info,
                           eosio::indexed_by<"prototalvote"_n, eosio::const_mem_fun<producer_info, double, &producer_info::by_votes>  >
                         > producers_table;


/** 
* redeem_requests table stores requests for redemptions
* should this be scoped by user and use timestamp of redemption period as key?
* or scoped by timestamp of redemption period and use user as key?
* pros to scope by redemption period: all data aggregated in 1 predictable place for each epoch
*/

struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] redeem_requests {
  eosio::name     wallet;
  eosio::asset    wax_amount_requested;
  
  uint64_t primary_key() const { return wallet.value; }
};
using requests_tbl = eosio::multi_index<"rdmrequests"_n, redeem_requests
>;


struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] snapshots {
  uint64_t          timestamp;
  eosio::asset      swax_earning_bucket;
  eosio::asset      lswax_autocompounding_bucket;
  eosio::asset      pol_bucket;
  eosio::asset      ecosystem_bucket;
  eosio::asset      total_distributed;
  
  uint64_t primary_key() const { return timestamp; }
};
using snaps_table = eosio::multi_index<"snapshots"_n, snapshots
>;


struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] stakers {
  eosio::name       wallet;
  eosio::asset      swax_balance;
  eosio::asset      claimable_wax;
  uint64_t          last_update;
  
  uint64_t primary_key() const { return wallet.value; }
};
using staker_table = eosio::multi_index<"stakers"_n, stakers
>;


struct [[eosio::table, eosio::contract(CONTRACT_NAME)]] state {
  eosio::asset      swax_currently_earning;
  eosio::asset      swax_currently_backing_lswax;
  eosio::asset      liquified_swax;
  eosio::asset      revenue_awaiting_distribution;
  eosio::asset      user_funds_bucket;
  eosio::asset      total_revenue_distributed;
  uint64_t          next_distribution;

  /* redemptions might be better off stored with epochs, since they are connected */
  eosio::asset      wax_for_redemption;
  uint64_t          redemption_period_start; 
  uint64_t          redemption_period_end;



  uint64_t          last_epoch_start_time;
  eosio::asset      wax_available_for_rentals;
  eosio::asset      cost_to_rent_1_wax;
  eosio::name       current_cpu_contract;
  uint64_t          next_stakeall_time;

  EOSLIB_SERIALIZE(state, (swax_currently_earning)
                          (swax_currently_backing_lswax)
                          (liquified_swax)
                          (revenue_awaiting_distribution)
                          (user_funds_bucket)
                          (total_revenue_distributed)
                          (next_distribution)
                          (wax_for_redemption)
                          (redemption_period_start)
                          (redemption_period_end)
                          (last_epoch_start_time)
                          (wax_available_for_rentals)
                          (cost_to_rent_1_wax)
                          (current_cpu_contract)
                          (next_stakeall_time)
                          )
};
using state_singleton = eosio::singleton<"state"_n, state>;