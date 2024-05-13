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

  	if( memo == "wax_lswax_liquidity" ){
  		check( tkcontract == WAX_CONTRACT, "only WAX should be sent with this memo" );
  		check( from == POL_CONTRACT, ( "expected " + POL_CONTRACT.to_string() + " to be the sender" ).c_str() );

  		issue_swax(quantity.amount);	

  		sync_epoch();  		

	    state s = states.get();
	    	
		double lsWAX_per_sWAX;

		//need to account for initial period where the values are still 0
		if( s.liquified_swax.amount == 0 && s.swax_currently_backing_lswax.amount == 0 ){
			lsWAX_per_sWAX = (double) 1;
		} else {
			lsWAX_per_sWAX = safeDivDouble((double) s.liquified_swax.amount, (double) s.swax_currently_backing_lswax.amount);
		}

		double converted_lsWAX_amount = safeMulDouble( lsWAX_per_sWAX, (double) quantity.amount );
		int64_t converted_lsWAX_i64 = (int64_t) converted_lsWAX_amount;	

		issue_lswax(converted_lsWAX_i64, _self);
		transfer_tokens( POL_CONTRACT, asset( converted_lsWAX_i64, LSWAX_SYMBOL ), TOKEN_CONTRACT, "liquidity" );

		s.swax_currently_backing_lswax.amount = safeAddInt64(s.swax_currently_backing_lswax.amount, quantity.amount);
		s.liquified_swax.amount = safeAddInt64(s.liquified_swax.amount, converted_lsWAX_i64);	  
		s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, quantity.amount);    

	    states.set(s, _self);
  		return;	    	
  	} 

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
	    s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, quantity.amount);

	    states.set(s, _self);

  		return;
  	}

  	/** unliquify memo
  	 *  used for converting lsWAX back to sWAX
  	 * 	rate is not 1:1, needs to be fetched from state table
  	 */

  	if( memo == "unliquify" ){
  		//front end has to package in a "stake" action before transferring, to make sure they have a row
  		sync_epoch();

  		check( tkcontract == TOKEN_CONTRACT, "only LSWAX can be unliquified" );

  		config c = configs.get();
  		check( quantity >= c.minimum_unliquify_amount, "minimum unliquify amount not met" );

  		//calculate the conversion rate (amount of sWAX to stake to this user)
  		state s = states.get();
  		double sWAX_per_lsWAX = safeDivDouble( (double) s.swax_currently_backing_lswax.amount, (double) s.liquified_swax.amount );
  		double converted_sWAX_amount = safeMulDouble( sWAX_per_lsWAX, (double) quantity.amount );
  		int64_t converted_sWAX_i64 = (int64_t) converted_sWAX_amount;

  		//retire the lsWAX AFTER figuring out the conversion rate
  		retire_lswax(quantity.amount);

  		//debit the amount from liquified sWAX
  		s.liquified_swax.amount = safeSubInt64(s.liquified_swax.amount, quantity.amount);
  		s.swax_currently_backing_lswax.amount = safeSubInt64(s.swax_currently_backing_lswax.amount, converted_sWAX_i64);

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

  	/** lp_incentives
  	 *  to be used as rewards for creating farms on alcor
  	 */ 

  	if( memo == "lp_incentives" ){
  		check( tkcontract == WAX_CONTRACT, "only WAX is accepted with lp_incentives memo" );
  		sync_epoch();

  		state s = states.get();
  		state2 s2 = state_s_2.get();

  		issue_swax(quantity.amount);
  		s.wax_available_for_rentals.amount = safeAddInt64( s.wax_available_for_rentals.amount, quantity.amount );

		double lsWAX_per_sWAX;

		//need to account for initial period where the values are still 0
		if( s.liquified_swax.amount == 0 && s.swax_currently_backing_lswax.amount == 0 ){
			lsWAX_per_sWAX = (double) 1;
		} else {
			lsWAX_per_sWAX = safeDivDouble((double) s.liquified_swax.amount, (double) s.swax_currently_backing_lswax.amount);
		}

		double converted_lsWAX_amount = safeMulDouble( lsWAX_per_sWAX, (double) quantity.amount );
		int64_t converted_lsWAX_i64 = (int64_t) converted_lsWAX_amount;	

		issue_lswax(converted_lsWAX_i64, _self);

  		s2.incentives_bucket.amount = safeAddInt64( s2.incentives_bucket.amount, converted_lsWAX_amount );

  		s.swax_currently_backing_lswax.amount = safeAddInt64( s.swax_currently_backing_lswax.amount, quantity.amount );
  		s.liquified_swax.amount = safeAddInt64(s.liquified_swax.amount, converted_lsWAX_i64);

  		states.set(s, _self);
  		state_s_2.set(s2, _self);

  		return;
  	}

  	/** cpu rental return
  	 * 	these are funds coming back from one of the CPU rental contracts after being staked/rented
  	 */

  	if( memo == "cpu rental return" ){
  		check( tkcontract == WAX_CONTRACT, "only WAX can be sent with this memo" );
  		sync_epoch();

  		config c = configs.get();
  		check( is_cpu_contract(from), "sender is not a valid cpu rental contract" );

  		state s = states.get();

  		/** 
  		* this SHOULD always belong to last epoch - 2 epochs
  		* i.e. by the time this arrives from epoch 1,
  		* it should now be epoch 3
  		*/

  		uint64_t relevant_epoch = s.last_epoch_start_time - ( c.cpu_rental_epoch_length_seconds * 2 );

  		//look up this epoch
  		auto epoch_itr = epochs_t.require_find(relevant_epoch, "could not locate relevant epoch");

  		while( epoch_itr->cpu_wallet != from ){
  			epoch_itr --;
  		}

  		//make sure the cpu contract is a match
  		check( epoch_itr->cpu_wallet == from, "sender does not match wallet linked to epoch" );

  		//add the relevant amount to the redemption bucket
  		asset total_added_to_redemption_bucket = epoch_itr->total_added_to_redemption_bucket;
  		asset amount_to_send_to_rental_bucket = quantity;

  		if( epoch_itr->total_added_to_redemption_bucket < epoch_itr->wax_to_refund ){
  			//there are still funds to add to redemption
  			int64_t amount_added = 0;

  			int64_t amount_remaining_to_add = safeSubInt64(epoch_itr->wax_to_refund.amount, epoch_itr->total_added_to_redemption_bucket.amount);

  			if(amount_remaining_to_add >= quantity.amount){
  				//the whole quantity will go to the redemption bucket
  				amount_added = quantity.amount;
  				
  			} else {
  				//only a portion of the amount will go to the redemption bucket
  				amount_added = amount_remaining_to_add;
  			}

  			total_added_to_redemption_bucket.amount = safeAddInt64(total_added_to_redemption_bucket.amount, amount_added);
  			amount_to_send_to_rental_bucket.amount = safeSubInt64(amount_to_send_to_rental_bucket.amount, amount_added);
  			s.wax_for_redemption.amount = safeAddInt64(s.wax_for_redemption.amount, amount_added);
  		}

  		//add the remainder to available for rental bucket
  		if(amount_to_send_to_rental_bucket.amount > 0){
  			s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, amount_to_send_to_rental_bucket.amount);
  		}

  		//update cpu funds returned in epoch table
  		asset total_cpu_funds_returned = epoch_itr->total_cpu_funds_returned;
  		total_cpu_funds_returned.amount = safeAddInt64(total_cpu_funds_returned.amount, quantity.amount);

  		epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
  			_e.total_cpu_funds_returned = total_cpu_funds_returned;
  			_e.total_added_to_redemption_bucket = total_added_to_redemption_bucket;
  		});

  		states.set(s, _self);
  		return;
  	}

  	/**
  	* @get_words
  	* anything below here should be a dynamic memo with multiple words to parse
  	*/

  	std::vector<std::string> words = get_words(memo);

  	/**
  	* rent_cpu
  	* if it has this, we need to parse it and find out which epoch, and who to rent to
  	*/ 

  	if( words[1] == "rent_cpu" ){
  		check( tkcontract == WAX_CONTRACT, "only WAX can be sent with this memo" );
  		check( words.size() >= 5, "memo for unliquify_exact operation is incomplete" );
  		sync_epoch();

  		//memo should also include account to rent to 
  		const eosio::name cpu_receiver = eosio::name( words[2] );
  		check( is_account( cpu_receiver ), ( cpu_receiver.to_string() + " is not an account" ).c_str() );

  		//memo should also include amount of wax to rent
  		const uint64_t wax_amount_to_rent = std::strtoull( words[3].c_str(), NULL, 0 );

  		//that amount should be > min_rental
  		check( wax_amount_to_rent >= MINIMUM_WAX_TO_RENT, ( "minimum wax amount to rent is " + std::to_string( MINIMUM_WAX_TO_RENT ) ).c_str() );
  		check( wax_amount_to_rent <= MAXIMUM_WAX_TO_RENT, ( "maximum wax amount to rent is " + std::to_string( MAXIMUM_WAX_TO_RENT ) ).c_str() );

  		//memo should include an epoch ID
  		const uint64_t epoch_id_to_rent_from = std::strtoull( words[4].c_str(), NULL, 0 );

  		state s = states.get();
  		config c = configs.get();

  		const uint64_t amount_to_rent_with_precision = safeMulUInt64(100000000, wax_amount_to_rent);

  		//make sure there is anough wax available for this rental
  		check( s.wax_available_for_rentals.amount >= amount_to_rent_with_precision, "there is not enough wax in the rental pool to cover this rental" );

  		//debit the wax from the rental pool
  		s.wax_available_for_rentals.amount = safeSubInt64(s.wax_available_for_rentals.amount, amount_to_rent_with_precision);

  		uint64_t eleven_days = 60 * 60 * 24 * 11;
  		double seconds_into_current_epoch = (double) now() - (double) s.last_epoch_start_time;
  		double days_into_current_epoch = safeDivDouble( seconds_into_current_epoch, ( (double) 60 * (double) 60 * (double) 24) );
  		double days_to_rent;

  		if( epoch_id_to_rent_from == s.last_epoch_start_time + c.seconds_between_epochs ){
  			//renting from epoch 3 (hasnt started yet)
  			days_to_rent = (double) 18 - days_into_current_epoch;

  			//see if this epoch exists yet - if it doesn't, create it
  			auto next_epoch_itr = epochs_t.find( epoch_id_to_rent_from );

  			if( next_epoch_itr == epochs_t.end() ){

  				uint64_t next_epoch_start_time = s.last_epoch_start_time + c.seconds_between_epochs;

				int next_cpu_index = 1;
				bool contract_was_found = false;

				for(eosio::name cpu : c.cpu_contracts){

					if( cpu == s.current_cpu_contract ){
					  contract_was_found = true;

					  if(next_cpu_index == c.cpu_contracts.size()){
					    next_cpu_index = 0;
					  }
					}

					if(contract_was_found) break;
					next_cpu_index ++;
				}

				check( contract_was_found, "error locating cpu contract" );
				eosio::name next_cpu_contract = c.cpu_contracts[next_cpu_index];
				check( next_cpu_contract != s.current_cpu_contract, "next cpu contract can not be the same as the current contract" );


  				epochs_t.emplace(_self, [&](auto &_e){
					_e.start_time = next_epoch_start_time;
					/* unstake 3 days before epoch ends */
					_e.time_to_unstake = next_epoch_start_time + c.cpu_rental_epoch_length_seconds - (60 * 60 * 24 * 3);
					_e.cpu_wallet = next_cpu_contract;
					_e.wax_bucket = ZERO_WAX;
					_e.wax_to_refund = ZERO_WAX;
					/* redemption starts at the end of the epoch, ends 48h later */
					_e.redemption_period_start_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds;
					_e.redemption_period_end_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds + c.redemption_period_length_seconds;
					_e.total_cpu_funds_returned = ZERO_WAX;
					_e.total_added_to_redemption_bucket = ZERO_WAX;
  				});
  			}

  		} else if( epoch_id_to_rent_from == s.last_epoch_start_time ){
  			//renting from epoch 2 (started most recently)
  			days_to_rent = (double) 11 - days_into_current_epoch;

  		} else if( epoch_id_to_rent_from == s.last_epoch_start_time - c.seconds_between_epochs ){
  			//renting from epoch 1 (oldest) and we need to make sure it's less than 11 days old  			
  			check( days_into_current_epoch < (double) 4, "it is too late to rent from this epoch, please rent from the next one" );

  			//if we reached here, minimum PAYMENT is 1 full day payment (even if rental is less than 1 day)
  			days_to_rent = (double) 4 - days_into_current_epoch < (double) 1 ? (double) 1 : (double) 4 - days_into_current_epoch;

  		} else{
  			check( false, "you are trying to rent from an invalid epoch" );
  		}

  		double expected_amount_received = safeMulDouble( (double) s.cost_to_rent_1_wax.amount, safeMulDouble( (double) wax_amount_to_rent, days_to_rent ) );

  		double amount_received_double = (double) quantity.amount;

  		check( amount_received_double >= expected_amount_received, ("expected to receive " + std::to_string(expected_amount_received) + " WAX" ).c_str() );

  		//if they sent more than expected, calculate difference and refund it
  		if( amount_received_double > expected_amount_received ){
  			//calculate difference
  			double amount_to_refund = amount_received_double - expected_amount_received;

  			if( (int64_t) amount_to_refund > 0 ){
  				transfer_tokens( from, asset( (int64_t) amount_to_refund, WAX_SYMBOL ), WAX_CONTRACT, "cpu rental refund from waxfusion.io - liquid staking protocol" );
  			}
  		}

  		auto epoch_itr = epochs_t.require_find( epoch_id_to_rent_from, ("epoch " + std::to_string(epoch_id_to_rent_from) + " does not exist").c_str() );

  		//send funds to the cpu contract
  		transfer_tokens( epoch_itr->cpu_wallet, asset( (int64_t) amount_to_rent_with_precision, WAX_SYMBOL), WAX_CONTRACT, cpu_stake_memo(cpu_receiver, epoch_id_to_rent_from) );

  		//update the epoch's wax_bucket
  		asset epoch_wax_bucket = epoch_itr->wax_bucket;
  		epoch_wax_bucket.amount = safeAddInt64(epoch_wax_bucket.amount, (int64_t) amount_to_rent_with_precision);

  		epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
  			_e.wax_bucket = epoch_wax_bucket;
  		});

  		//update the state
  		states.set(s, _self);
  		return;
  	}

  	if( words[1] == "unliquify_exact" ){

  		check( words.size() >= 4, "memo for unliquify_exact operation is incomplete" );
  		check( tkcontract == TOKEN_CONTRACT, "only LSWAX can be unliquified" );
  		sync_epoch();

  		config c = configs.get();
  		check( quantity >= c.minimum_unliquify_amount, "minimum unliquify amount not met" );

  		const uint64_t expected_output = std::strtoull( words[2].c_str(), NULL, 0 );
  		const double max_slippage = std::stod( words[3].c_str() );

  		//calculate the conversion rate (amount of sWAX to stake to this user)
  		state s = states.get();
  		double sWAX_per_lsWAX = safeDivDouble((double) s.swax_currently_backing_lswax.amount, (double) s.liquified_swax.amount);
  		double converted_sWAX_amount = safeMulDouble( sWAX_per_lsWAX, (double) quantity.amount );
  		int64_t converted_sWAX_i64 = (int64_t) converted_sWAX_amount;

		check( max_slippage >= (double) 0 && max_slippage < (double) 1, "max slippage is out of range" );
		check( expected_output > (uint64_t) 0 && expected_output <= MAX_ASSET_AMOUNT_U64, "expected output is out of range" );
		double minimum_output_percentage = (double) 1 - max_slippage;
		double minimum_output = safeMulDouble( (double) expected_output, minimum_output_percentage );

		check( converted_sWAX_i64 >= (int64_t) minimum_output, "output would be " + asset(converted_sWAX_i64, SWAX_SYMBOL).to_string() + " but expected " + asset(minimum_output, SWAX_SYMBOL).to_string() );	  		

  		retire_lswax(quantity.amount);

  		//debit the amount from liquified sWAX
  		s.liquified_swax.amount = safeSubInt64(s.liquified_swax.amount, quantity.amount);
  		s.swax_currently_backing_lswax.amount = safeSubInt64(s.swax_currently_backing_lswax.amount, converted_sWAX_i64);

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

}