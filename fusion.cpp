#include "fusion.hpp"
#include "functions.cpp"
#include "safe.cpp"
#include "on_notify.cpp"

ACTION fusion::claimrewards(const eosio::name& user){
	/* converting a % to powerup should be bundled in on the front end (i.e. ignored here) */

	require_auth(user);
	sync_user(user);

	auto staker = staker_t.require_find(user.value, "you don't have anything staked here");
	if(staker->claimable_wax.amount > 0){
		transfer_tokens( user, staker->claimable_wax, WAX_CONTRACT, std::string("your sWAX reward claim from waxfusion.io") );

		staker_t.modify(staker, same_payer, [&](auto &_s){
			_s.claimable_wax.amount = 0;
		});

		return;
	}

	check(false, "you have nothing to claim");
}

ACTION fusion::distribute(){
	//should anyone be able to call this?

	//get the config
	config c = configs.get();

	//get the state
	state s = states.get();

	//make sure its been long enough since the last distribution
	if( s.next_distribution > now() ){
		check( false, ("next distribution is not until " + std::to_string(s.next_distribution) ).c_str() );
	}

	if( s.revenue_awaiting_distribution.amount == 0 ){
		//do a zero distribution (avoid conditional safemath below)
		zero_distribution();
		return;
	}

	//amount to distribute = revenue_awaiting_distribution
	double amount_to_distribute = (double) s.revenue_awaiting_distribution.amount;
	double user_allocation = amount_to_distribute * c.user_share;
	double pol_allocation = amount_to_distribute * c.pol_share;
	double ecosystem_share = amount_to_distribute - user_allocation - pol_allocation;

	double sum_of_sWAX_and_lsWAX = (double) s.swax_currently_earning.amount + (double) s.swax_currently_backing_lswax.amount;

	double swax_currently_earning_allocation = 
		user_allocation 
		* 
		safeDivDouble( (double) s.swax_currently_earning.amount, sum_of_sWAX_and_lsWAX );

	double autocompounding_allocation = 
		user_allocation 
		* 
		safeDivDouble( (double) s.swax_currently_backing_lswax.amount, sum_of_sWAX_and_lsWAX );		

	//issue sWAX
	issue_swax( (int64_t) autocompounding_allocation );

	//increase the backing of lsWAX with the newly issued sWAX
	s.swax_currently_backing_lswax.amount = safeAddInt64( s.swax_currently_backing_lswax.amount, (int64_t) autocompounding_allocation );

	//allocate this WAX the same way that it would be handled if we received a "stake" transfer





	/** 
	* TODO: add more safety checks now that the addition of allocating user funds between s/lsWAX has taken place
	* also need more safemath functions, specifically for adding doubles and multiplying int64_t / double
	*/



	//i64 allocations
	int64_t amount_to_distribute_i64 = (int64_t) amount_to_distribute;
	int64_t user_alloc_i64 = (int64_t) user_allocation;
	int64_t swax_earning_alloc_i64 = (int64_t) swax_currently_earning_allocation;
	int64_t swax_autocompounding_alloc_i64 = (int64_t) autocompounding_allocation;
	int64_t pol_alloc_i64 = (int64_t) pol_allocation;
	int64_t eco_alloc_i64 = (int64_t) ecosystem_share;

	//check the sum is in range
	int64_t alloc_check_1 = safeAddInt64(user_alloc_i64, pol_alloc_i64);
	int64_t alloc_check_2 = safeAddInt64(alloc_check_1, eco_alloc_i64);
	check( alloc_check_2 <= amount_to_distribute_i64, "allocation check 2 failed" );

	//set revenue_awaiting_distribution to 0
	s.revenue_awaiting_distribution.amount = 0;

	//user share goes to s.user_funds_bucket
	s.user_funds_bucket.amount = safeAddInt64(s.user_funds_bucket.amount, swax_earning_alloc_i64);

	//pol share goes to POL_CONTRACT
	transfer_tokens( POL_CONTRACT, asset(pol_alloc_i64, WAX_SYMBOL), WAX_CONTRACT, std::string("pol allocation from waxfusion distribution") );

	//loop through ecosystem_fund and store each of their share in ecosystem table 

	double total_paid_to_eco = 0;
	for(auto e : c.ecosystem_fund){
		double allocation = e.amount * ecosystem_share;
		int64_t allocation_i64 = (int64_t) allocation;
		total_paid_to_eco += allocation;

		auto eco_it = eco_t.find(e.beneficiary.value);
		if( eco_it == eco_t.end() ){
			eco_t.emplace(get_self(), [&](auto &_eco){
				_eco.beneficiary = e.beneficiary;
				_eco.wax_balance = asset(allocation_i64, WAX_SYMBOL);
				_eco.total_wax_received = asset(allocation_i64, WAX_SYMBOL);		
			});
		} else {
			int64_t new_balance = safeAddInt64(eco_it->wax_balance.amount, allocation_i64);
			int64_t updated_total = safeAddInt64(eco_it->total_wax_received.amount, allocation_i64);

			eco_t.modify(eco_it, same_payer, [&](auto &_eco){
				_eco.wax_balance = asset(new_balance, WAX_SYMBOL);
				_eco.total_wax_received = asset(updated_total, WAX_SYMBOL);
			});
		}
	}

	check( total_paid_to_eco <= ecosystem_share, "overdrawn ecosystem allocation" );

	//create a snapshot
	snaps_t.emplace(get_self(), [&](auto &_snap){
		_snap.timestamp = s.next_distribution;
		_snap.swax_earning_bucket = asset(swax_earning_alloc_i64, WAX_SYMBOL);
		_snap.lswax_autocompounding_bucket = asset(swax_autocompounding_alloc_i64, WAX_SYMBOL);
		_snap.pol_bucket = asset(pol_alloc_i64, WAX_SYMBOL);
		_snap.ecosystem_bucket = asset(eco_alloc_i64, WAX_SYMBOL);
		_snap.total_distributed = asset(amount_to_distribute_i64, WAX_SYMBOL);	
	});

	//update total_revenue_distributed in state
	s.total_revenue_distributed.amount = safeAddInt64(s.total_revenue_distributed.amount, amount_to_distribute_i64);

	//update next_dist in the state table
	s.next_distribution += c.seconds_between_distributions;

	states.set(s, _self);
}

ACTION fusion::initconfig(){
	require_auth(get_self());

	eosio::check(!configs.exists(), "Config already exists");
	eosio::check(!states.exists(), "State already exists");

	double eco_split = 1 / 6;

	config c{};
	c.minimum_stake_amount = eosio::asset(100000000, WAX_SYMBOL);
	c.minimum_unliquify_amount = eosio::asset(100000000, LSWAX_SYMBOL);
	c.seconds_between_distributions = 86400;
	c.max_snapshots_to_process = 180;
	c.initial_epoch_start_time = INITIAL_EPOCH_START_TIMESTAMP;
	c.cpu_rental_epoch_length_seconds = 60 * 60 * 24 * 14; /* 14 days */
	c.seconds_between_epochs = 60 * 60 * 24 * 7; /* 7 days */
	c.user_share = 0.85;
	c.pol_share = 0.1;
	c.ecosystem_fund = {
		{"nefty"_n, eco_split },
		{"hive"_n, eco_split },
		{"waxdao"_n, eco_split },
		{"wombat"_n, eco_split },
		{"taco"_n, eco_split },
		{"alcor"_n, eco_split }
	};
	configs.set(c, _self);

	state s{};
	s.swax_currently_earning = ZERO_SWAX;
	s.swax_currently_backing_lswax = ZERO_SWAX;
	s.liquified_swax = ZERO_LSWAX;
	s.revenue_awaiting_distribution = ZERO_WAX;
	s.user_funds_bucket = ZERO_WAX;
	s.total_revenue_distributed = ZERO_WAX;
	s.next_distribution = now();
	s.wax_for_redemption = ZERO_WAX;
	s.redemption_period_start = 0;
	s.redemption_period_end = 0;
	s.last_epoch_start_time = INITIAL_EPOCH_START_TIMESTAMP;
	s.wax_available_for_rentals = ZERO_WAX;
	s.cost_to_rent_1_wax = asset(1000000, WAX_SYMBOL); /* 0.01 WAX per day */
	states.set(s, _self);
}

ACTION fusion::liquify(const eosio::name& user, const eosio::asset& quantity){
	require_auth(user);
    check(quantity.amount > 0, "Invalid quantity.");
    check(quantity.amount < MAX_ASSET_AMOUNT, "quantity too large");
    check(quantity.symbol == SWAX_SYMBOL, "only SWAX can be liquified");	

    //process any payouts for this user since their last interaction
    sync_user(user);

    //make sure they have a row here
	auto staker = staker_t.require_find(user.value, "you have nothing to liquify");

	if(staker->swax_balance < quantity){
		check(false, "you are trying to liquify more than you have");
	}

	//debit requested amount from their staked balance
	staker_t.modify(staker, same_payer, [&](auto &_s){
		_s.swax_balance -= quantity;
		_s.last_update = now();
	});

	//get the current state table
	state s = states.get();

	//calculate equivalent amount of lsWAX (BEFORE adjusting sWAX amounts)
	double lsWAX_per_sWAX = safeDivDouble((double) s.liquified_swax.amount, (double) s.swax_currently_backing_lswax.amount);
	double converted_lsWAX_amount = lsWAX_per_sWAX * (double) quantity.amount;
	int64_t converted_lsWAX_i64 = (int64_t) converted_lsWAX_amount;	

	//subtract swax amount from swax_currently_earning
	s.swax_currently_earning.amount = safeSubInt64(s.swax_currently_earning.amount, quantity.amount);

	//add swax amount to swax_currently_backing_swax
	s.swax_currently_backing_lswax.amount = safeAddInt64(s.swax_currently_backing_lswax.amount, quantity.amount);

	//issue the converted lsWAX amount to the user
	issue_lswax(converted_lsWAX_i64, user);

	//add the issued amount to liquified_swax
	s.liquified_swax.amount = safeAddInt64(s.liquified_swax.amount, converted_lsWAX_i64);

	//apply the changes to state table
	states.set(s, _self);

	return;
}

ACTION fusion::stake(const eosio::name& user){
	require_auth(user);

	auto staker = staker_t.find(user.value);

	if(staker != staker_t.end()){
		sync_user(user);
		return;
	}

	staker_t.emplace(user, [&](auto &_s){
		_s.wallet = user;
		_s.swax_balance = ZERO_SWAX;
		_s.claimable_wax = ZERO_WAX;
		_s.last_update = now();
	});
}