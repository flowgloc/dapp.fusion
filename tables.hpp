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