#pragma once

void fusion::receive_token_transfer(name from, name to, eosio::asset quantity, std::string memo){
	const name tkcontract = get_first_receiver();

    check( quantity.amount > 0, "Must redeem a positive quantity" );
    check( quantity.amount < MAX_ASSET_AMOUNT, "quantity too large" );

    if( from == get_self() || to != get_self() ){
    	return;
    }

    //accept random tokens but dont execute any logic
    if( !memo_is_expected(memo) ) return;

    //only accept wax and lsWAX (sWAX is only issued, not transferred)
  	validate_token(quantity.symbol, tkcontract);

  	/** 
  	 *  why might we receive tokens?
  	 * 	WAX, to mint sWAX (memo == stake)
  	 * 	lsWAX, to unliquify and convert to sWAX (memo == unliquify)
  	 * 	WAX, to rent CPU (memo == rent) 
  	 * 	WAX, when claiming voting rewards (that likely wont happen on this contract)
  	 * 	but we will receive deposits of rewards from other contracts
  	 * 	so...
  	 * 	"revenue" sent here from.. anything (memo == waxfusion_revenue)
  	 */

  	/** stake memo
  	 *  used for creating new sWAX tokens at 1:1 ratio
  	 * 	these sWAX will be staked initially, (added to the awaiting_new_epoch balance)
  	 * 	they can be converted to liquid sWAX (lsWAX) afterwards
  	 */

  	if( memo == "stake" ){
  		check( tkcontract == WAX_CONTRACT, "only WAX is used for staking" );
  		
  		config c = configs.get();
  		check( quantity >= c.minimum_stake_amount, "minimum stake amount not met" );

  		//issue new sWAX to dapp contract
  		issue_swax(quantity.amount);   		

	    //sync user (need a function to "catch them up" if necessary)
	    sync_user(from);

  		//fetch the refreshed iterator after syncing
  		auto staker = staker_t.require_find(from.value, "you need to use the stake action first");

  		//add the deposit amount to their staked sWAX
  		eosio::asset staked_balance = staker->swax_balance;
  		staked_balance.amount = safeAddInt64(staked_balance.amount, quantity.amount);

  		staker_t.modify(staker, same_payer, [&](auto &_s){
  			_s.swax_balance = staked_balance;
  			_s.last_update = now();
  		});

  		sync_epoch();

  		//add this amount to the "currently_earning" sWAX bucket
  		//state should not be fetched until after epoch is synced
	    state s = states.get();
	    s.swax_currently_earning.amount = safeAddInt64(s.swax_currently_earning.amount, quantity.amount);
	    s.wax_available_for_rentals += quantity;

	    //check when the last epoch started, calculate next epoch start
	    //upsert epoch and add this to the bucket
	    uint64_t next_epoch_start_time = s.last_epoch_start_time += c.seconds_between_epochs;
	    auto epoch_itr = epochs_t.find(next_epoch_start_time);

	    if(epoch_itr == epochs_t.end()){
	    	epochs_t.emplace(get_self(), [&](auto &_e){
	    		_e.start_time = next_epoch_start_time;
	    		/* unstake 3 days before epoch ends */
	    		_e.time_to_unstake = next_epoch_start_time + c.cpu_rental_epoch_length_seconds - (60 * 60 * 24 * 3);
	    		_e.cpu_wallet = "idk"_n; /* how do we figure this out, and do we transfer there now? */
	    		_e.wax_bucket = quantity;
	    		_e.wax_to_refund = ZERO_WAX;
	    		/* redemption starts at the end of the epoch, ends 48h later */
	    		_e.redemption_period_start_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds;
	    		_e.redemption_period_end_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds + (60 * 60 * 48);
	    	});
	    } else {
	    	/* TODO: safemath for the addition to wax_bucket */

	    	epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
	    		_e.wax_bucket += quantity;
	    	});
	    }

	    states.set(s, _self);

  		return;

  		//the liquify action can be packaged into this tx on the front end
  	}

  	/** unliquify memo
  	 *  used for converting lsWAX back to sWAX
  	 * 	rate is not 1:1, needs to be fetched from state table
  	 */

  	if( memo == "unliquify" ){
  		//front end has to package in a "stake" action before transferring, to make sure they have a row

  		check( tkcontract == TOKEN_CONTRACT, "only LSWAX can be unliquified" );

  		config c = configs.get();
  		check( quantity >= c.minimum_unliquify_amount, "minimum unliquify amount not met" );

  		//calculate the conversion rate (amount of sWAX to stake to this user)
  		state s = states.get();
  		double sWAX_per_lsWAX = safeDivDouble((double) s.swax_currently_backing_lswax.amount, (double) s.liquified_swax.amount);
  		double converted_sWAX_amount = sWAX_per_lsWAX * (double) quantity.amount;
  		int64_t converted_sWAX_i64 = (int64_t) converted_sWAX_amount;

  		//retire the lsWAX AFTER figuring out the conversion rate
  		retire_lswax(converted_sWAX_i64);

  		//debit the amount from liquified sWAX
  		s.liquified_swax.amount = safeSubInt64(s.liquified_swax.amount, converted_sWAX_i64);

  		//add this amount to the "currently_earning" sWAX bucket
  		s.swax_currently_earning.amount = safeAddInt64(s.swax_currently_earning.amount, converted_sWAX_i64);
	    
	    states.set(s, _self);   

	    //sync this user before adjusting their row
  		sync_user(from);

  		//proceed with adding this staked sWAX to their balance
  		auto staker = staker_t.require_find(from.value, "you need to use the stake action first");
  		eosio::asset staked_balance = staker->swax_balance;
  		staked_balance.amount = safeAddInt64(staked_balance.amount, converted_sWAX_i64);

  		staker_t.modify(staker, same_payer, [&](auto &_s){
  			_s.swax_balance = staked_balance;
  			_s.last_update = now();
  		});

  		return;

  	}

  	/** waxfusion_revenue
  	 * 	used for receiving revenue from helper contracts, like CPU rentals, wax staking, etc
  	 */

  	if( memo == "waxfusion_revenue" ){
  		check( tkcontract == WAX_CONTRACT, "only WAX is accepted with waxfusion_revenue memo" );

  		//add the wax to state.revenue_awaiting_distribution
  		state s = states.get();
  		s.revenue_awaiting_distribution.amount = safeAddInt64(s.revenue_awaiting_distribution.amount, quantity.amount);
  		states.set(s, _self); 
  		return;
  	}

}