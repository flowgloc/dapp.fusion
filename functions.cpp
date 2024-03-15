#pragma once

void fusion::issue_lswax(const int64_t& amount, const eosio::name& receiver){
  action(permission_level{get_self(), "active"_n}, TOKEN_CONTRACT,"issue"_n,std::tuple{ get_self(), receiver, eosio::asset(amount, LSWAX_SYMBOL), std::string("issuing lsWAX to liquify")}).send();
  return;
}

void fusion::issue_swax(const int64_t& amount){
  action(permission_level{get_self(), "active"_n}, TOKEN_CONTRACT,"issue"_n,std::tuple{ get_self(), get_self(), eosio::asset(amount, SWAX_SYMBOL), std::string("issuing sWAX for staking")}).send();
  return;
}

bool fusion::memo_is_expected(const std::string& memo){
  if( memo == "stake" || memo == "unliquify" || memo == "waxfusion_revenue" ) return true;
  return false;
}

uint64_t fusion::now(){
  return current_time_point().sec_since_epoch();
}

void fusion::retire_lswax(const int64_t& amount){
  action(permission_level{get_self(), "active"_n}, TOKEN_CONTRACT,"retire"_n,std::tuple{ get_self(), eosio::asset(amount, LSWAX_SYMBOL), std::string("retiring lsWAX to unliquify")}).send();
  return;
}

void fusion::sync_epoch(){
  //find out when the last epoch started
  state s = states.get();
  config c = configs.get();

  //calculate when the next is supposed to start
  uint64_t next_epoch_start_time = s.last_epoch_start_time += c.seconds_between_epochs;

  //is now() >= that?
  if( now() >= next_epoch_start_time ){
    //if so, update last epoch start time
    s.last_epoch_start_time = next_epoch_start_time;
    states.set(s, _self);
  }

  return;
}

void fusion::sync_user(const eosio::name& user){
  auto staker = staker_t.require_find(user.value, "you need to use the stake action first");

  //is their last_update < last_payout ?
  auto low_itr = snaps_t.lower_bound(staker->last_update);

  if(low_itr == snaps_t.end()) return;

  config c = configs.get();
  state s = states.get();

  int count = 0;
  int64_t wax_owed_to_user = 0; 

  /* if they have no staked sWAX, they get 0 */
  if(staker->swax_balance.amount > 0){

    for(auto it = low_itr; it != snaps_t.end(); it++){
      //if the amount to pay out is > 0...
      if(it->swax_earning_bucket.amount > 0){
        //calculate the % of snapshot owned by this user
        double percentage_allocation = (double) staker->swax_balance.amount / (double) it->swax_earning_bucket.amount; 

        //use that to get a wax quantity (needs to be positive)
        double wax_allocation = (double) it->swax_earning_bucket.amount * percentage_allocation;
       
        //add that quantity to wax_owed_to_user
        wax_owed_to_user = safeAddInt64(wax_owed_to_user, (int64_t) wax_allocation);
      }

      count ++;
      if(count >= c.max_snapshots_to_process) break;
    }
  }

  asset claimable_wax = staker->claimable_wax;
  claimable_wax.amount = safeAddInt64(claimable_wax.amount, wax_owed_to_user);

  //credit their balance, update last_update to now()
  staker_t.modify(staker, same_payer, [&](auto &_s){
    _s.claimable_wax = claimable_wax;
    _s.last_update = now();
  });

  //debit the user bucket in state
  s.user_funds_bucket.amount = safeSubInt64(s.user_funds_bucket.amount, wax_owed_to_user);
  states.set(s, _self);

  return;
}

void fusion::transfer_tokens(const name& user, const asset& amount_to_send, const name& contract, const std::string& memo){
  action(permission_level{get_self(), "active"_n}, contract,"transfer"_n,std::tuple{ get_self(), user, amount_to_send, memo}).send();
  return;
}

void fusion::validate_token(const eosio::symbol& symbol, const eosio::name& contract)
{
  if(symbol == WAX_SYMBOL){
    check(contract == WAX_CONTRACT, "expected eosio.token contract");
    return;
  }

  if(symbol == LSWAX_SYMBOL){
    check(contract == TOKEN_CONTRACT, "expected token.fusion contract");
    return;
  }  

  check(false, "invalid token received");
}

void fusion::zero_distribution(){
  config c = configs.get();
  state s = states.get();

  snaps_t.emplace(get_self(), [&](auto &_snap){
    _snap.timestamp = s.next_distribution;
    _snap.swax_earning_bucket = ZERO_WAX;
    _snap.lswax_autocompounding_bucket = ZERO_WAX;
    _snap.pol_bucket = ZERO_WAX;
    _snap.ecosystem_bucket = ZERO_WAX;
    _snap.total_distributed = ZERO_WAX;  
  });  

  s.next_distribution += c.seconds_between_distributions;
  states.set(s, _self);    
}