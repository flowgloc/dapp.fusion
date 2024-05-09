#include "fusion.hpp"
#include "functions.cpp"
#include "safe.cpp"
#include "on_notify.cpp"


ACTION fusion::addadmin(const eosio::name& admin_to_add){
	require_auth(_self);
	check( is_account(admin_to_add), "admin_to_add is not a wax account" );

	config c = configs.get();

	if( std::find( c.admin_wallets.begin(), c.admin_wallets.end(), admin_to_add ) == c.admin_wallets.end() ){
		c.admin_wallets.push_back( admin_to_add );
		configs.set(c, _self);
	} else {
		check( false, ( admin_to_add.to_string() + " is already an admin" ).c_str() );
	}
}

ACTION fusion::addcpucntrct(const eosio::name& contract_to_add){
	require_auth(_self);
	check( is_account(contract_to_add), "contract_to_add is not a wax account" );

	config c = configs.get();

	if( std::find( c.cpu_contracts.begin(), c.cpu_contracts.end(), contract_to_add ) == c.cpu_contracts.end() ){
		c.cpu_contracts.push_back( contract_to_add );
		configs.set(c, _self);
	} else {
		check( false, ( contract_to_add.to_string() + " is already a cpu contract" ).c_str() );
	}
}

ACTION fusion::claimgbmvote(const eosio::name& cpu_contract)
{
	check( is_cpu_contract(cpu_contract), ( cpu_contract.to_string() + " is not a cpu rental contract").c_str() );
	action(permission_level{get_self(), "active"_n}, cpu_contract,"claimgbmvote"_n,std::tuple{}).send();
}

ACTION fusion::claimrefunds()
{
	//anyone can call this

	config c = configs.get();

	bool refundsToClaim = false;

	for(eosio::name ctrct : c.cpu_contracts){
		refunds_table refunds_t = refunds_table( SYSTEM_CONTRACT, ctrct.value );

		auto refund_itr = refunds_t.find( ctrct.value );

		if( refund_itr != refunds_t.end() && refund_itr->request_time + seconds(REFUND_DELAY_SEC) <= current_time_point() ){
			action(permission_level{get_self(), "active"_n}, ctrct,"claimrefund"_n,std::tuple{}).send();
			refundsToClaim = true;
		}

	}

	check( refundsToClaim, "there are no refunds to claim" );
}

ACTION fusion::claimaslswax(const eosio::name& user, const eosio::asset& expected_output, const double& max_slippage){
	require_auth(user);
	sync_epoch();
	sync_user(user);

    check(expected_output.amount > 0, "Invalid output quantity.");
    check(expected_output.amount < MAX_ASSET_AMOUNT, "output quantity too large");
    check(expected_output.symbol == LSWAX_SYMBOL, "output symbol should be LSWAX");	 	

	auto staker = staker_t.require_find(user.value, "you don't have anything staked here");
	if(staker->claimable_wax.amount > 0){

		int64_t claimable_wax_amount = staker->claimable_wax.amount;
		issue_swax(claimable_wax_amount);

		state s = states.get();

		double lsWAX_per_sWAX;

		//need to account for initial period where the values are still 0
		if( s.liquified_swax.amount == 0 && s.swax_currently_backing_lswax.amount == 0 ){
			lsWAX_per_sWAX = (double) 1;
		} else {
			lsWAX_per_sWAX = safeDivDouble((double) s.liquified_swax.amount, (double) s.swax_currently_backing_lswax.amount);
		}

		double converted_lsWAX_amount = safeMulDouble( lsWAX_per_sWAX, (double) claimable_wax_amount );
		int64_t converted_lsWAX_i64 = (int64_t) converted_lsWAX_amount;	

	    check( max_slippage >= (double) 0 && max_slippage < (double) 1, "max slippage is out of range" );
	    double minimum_output_percentage = (double) 1 - max_slippage;
	    double minimum_output = safeMulDouble( (double) expected_output.amount, minimum_output_percentage );

	    check( converted_lsWAX_i64 >= (int64_t) minimum_output, "output would be " + asset(converted_lsWAX_i64, LSWAX_SYMBOL).to_string() + " but expected " + asset(minimum_output, LSWAX_SYMBOL).to_string() );		

		issue_lswax(converted_lsWAX_i64, user);

		staker_t.modify(staker, same_payer, [&](auto &_s){
			_s.claimable_wax.amount = 0;
		});

		s.liquified_swax.amount = safeAddInt64(s.liquified_swax.amount, converted_lsWAX_i64);
		s.swax_currently_backing_lswax.amount = safeAddInt64(s.swax_currently_backing_lswax.amount, claimable_wax_amount);
	    s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, claimable_wax_amount);

	    states.set(s, _self);		

		return;
	}

	check(false, "you have nothing to claim");
}

ACTION fusion::claimrewards(const eosio::name& user){
	require_auth(user);
	sync_epoch();
	sync_user(user);

	auto staker = staker_t.require_find(user.value, "you don't have anything staked here");
	if(staker->claimable_wax.amount > 0){
		transfer_tokens( user, staker->claimable_wax, WAX_CONTRACT, std::string("your sWAX reward claim from waxfusion.io - liquid staking protocol") );

		staker_t.modify(staker, same_payer, [&](auto &_s){
			_s.claimable_wax.amount = 0;
		});

		return;
	}

	check(false, "you have nothing to claim");
}

/** 
* claimswax
* allows a user to autocompound their sWAX by claiming WAX and turning it back into more sWAX 
*/

ACTION fusion::claimswax(const eosio::name& user){
	require_auth(user);
	sync_epoch();
	sync_user(user);

	auto staker = staker_t.require_find(user.value, "you don't have anything staked here");
	if(staker->claimable_wax.amount > 0){

		int64_t swax_amount_to_claim = staker->claimable_wax.amount;
		issue_swax(swax_amount_to_claim); 

		staker_t.modify(staker, same_payer, [&](auto &_s){
			_s.claimable_wax.amount = 0;
			_s.swax_balance.amount = safeAddInt64(_s.swax_balance.amount, swax_amount_to_claim);
		});

		//update the state
	    state s = states.get();
	    s.swax_currently_earning.amount = safeAddInt64(s.swax_currently_earning.amount, swax_amount_to_claim);
	    s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, swax_amount_to_claim);

	    states.set(s, _self);		

		return;
	}

	check(false, "you have nothing to claim");
}

/**
* clearexpired
* allows a user to erase their expired redemption requests from the redemptions table
*/  

ACTION fusion::clearexpired(const eosio::name& user){
	require_auth(user);
	sync_epoch();
	sync_user(user);

	config c = configs.get();
	state s = states.get();	
	requests_tbl requests_t = requests_tbl(get_self(), user.value);

	if( requests_t.begin() == requests_t.end() ) return;

	uint64_t upper_bound = s.last_epoch_start_time - c.seconds_between_epochs - 1;

	auto itr = requests_t.begin();
	while (itr != requests_t.end()) {
		if (itr->epoch_id >= upper_bound) break;

		itr = requests_t.erase(itr);
	}

}

/**
* distribute action
* anyone can call this as long as 24 hours have passed since the last reward distribution
*/ 

ACTION fusion::distribute(){
	sync_epoch();

	//get the config
	config c = configs.get();

	//get the state
	state s = states.get();

	//make sure its been long enough since the last distribution
	if( s.next_distribution > now() ){
		check( false, ("next distribution is not until " + std::to_string(s.next_distribution) ).c_str() );
	}

	if( s.revenue_awaiting_distribution.amount == 0 ){
		zero_distribution();
		return;
	}

	//amount to distribute = revenue_awaiting_distribution
	double amount_to_distribute = (double) s.revenue_awaiting_distribution.amount;
	double user_allocation = safeMulDouble(amount_to_distribute, c.user_share);
	double pol_allocation = safeMulDouble(amount_to_distribute, c.pol_share);
	double ecosystem_share = amount_to_distribute - user_allocation - pol_allocation;

	//TODO: safeAddDouble
	double sum_of_sWAX_and_lsWAX = safeAddDouble( (double) s.swax_currently_earning.amount, (double) s.swax_currently_backing_lswax.amount );

	double swax_currently_earning_allocation = 
		safeMulDouble( user_allocation, 
			safeDivDouble( (double) s.swax_currently_earning.amount, sum_of_sWAX_and_lsWAX ) 
			);

	double autocompounding_allocation = 
		safeMulDouble( user_allocation, 
			safeDivDouble( (double) s.swax_currently_backing_lswax.amount, sum_of_sWAX_and_lsWAX )
			);	

	//issue sWAX
	issue_swax( (int64_t) autocompounding_allocation );

	//increase the backing of lsWAX with the newly issued sWAX
	s.swax_currently_backing_lswax.amount = safeAddInt64( s.swax_currently_backing_lswax.amount, (int64_t) autocompounding_allocation );

	//i64 allocations
	int64_t amount_to_distribute_i64 = (int64_t) amount_to_distribute;
	int64_t user_alloc_i64 = (int64_t) user_allocation;
	int64_t pol_alloc_i64 = (int64_t) pol_allocation;
	int64_t eco_alloc_i64 = (int64_t) ecosystem_share;
	int64_t swax_earning_alloc_i64 = (int64_t) swax_currently_earning_allocation;
	int64_t swax_autocompounding_alloc_i64 = (int64_t) autocompounding_allocation;
	
	//check the sum is in range
	int64_t alloc_check_1 = safeAddInt64(user_alloc_i64, pol_alloc_i64);
	int64_t alloc_check_2 = safeAddInt64(alloc_check_1, eco_alloc_i64);
	int64_t alloc_check_3 = safeAddInt64(alloc_check_2, swax_earning_alloc_i64);
	int64_t alloc_check_4 = safeAddInt64(alloc_check_3, swax_autocompounding_alloc_i64);
	check( alloc_check_4 <= amount_to_distribute_i64, "allocation check 4 failed" );

	//set revenue_awaiting_distribution to 0
	s.revenue_awaiting_distribution.amount = 0;

	//user share goes to s.user_funds_bucket
	s.user_funds_bucket.amount = safeAddInt64(s.user_funds_bucket.amount, swax_earning_alloc_i64);

	//pol share goes to POL_CONTRACT
	transfer_tokens( POL_CONTRACT, asset(pol_alloc_i64, WAX_SYMBOL), WAX_CONTRACT, std::string("pol allocation from waxfusion distribution") );

	//send ecosystem allocation to another contract to keep logic abstracted
	transfer_tokens( VE33_CONTRACT, asset(eco_alloc_i64, WAX_SYMBOL), WAX_CONTRACT, std::string("revenue") );

	//create a snapshot
	snaps_t.emplace(get_self(), [&](auto &_snap){
		_snap.timestamp = s.next_distribution;
		_snap.swax_earning_bucket = asset(swax_earning_alloc_i64, WAX_SYMBOL);
		_snap.lswax_autocompounding_bucket = asset(swax_autocompounding_alloc_i64, WAX_SYMBOL);
		_snap.pol_bucket = asset(pol_alloc_i64, WAX_SYMBOL);
		_snap.ecosystem_bucket = asset(eco_alloc_i64, WAX_SYMBOL);
		_snap.total_distributed = asset(amount_to_distribute_i64, WAX_SYMBOL);	
		_snap.total_swax_earning = s.swax_currently_earning;
	});

	//update total_revenue_distributed in state
	s.total_revenue_distributed.amount = safeAddInt64(s.total_revenue_distributed.amount, amount_to_distribute_i64);

	//update next_dist in the state table
	s.next_distribution += c.seconds_between_distributions;

    s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, swax_autocompounding_alloc_i64);

    states.set(s, _self);

	return;	

}


ACTION fusion::initconfig(){
	require_auth(get_self());

	eosio::check(!configs.exists(), "Config already exists");
	eosio::check(!states.exists(), "State already exists");

	double eco_split = (double) 1 / (double) 6;

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
	c.admin_wallets = {
		"guild.waxdao"_n,
		"oig"_n,
		"workr.fusion"_n,
		_self
		//"admin.wax"_n
	};
	c.cpu_contracts = {
		"cpu1.fusion"_n,
		"cpu2.fusion"_n,
		"cpu3.fusion"_n
	};
	c.redemption_period_length_seconds = 60 * 60 * 24 * 2; /* 2 days */
	c.seconds_between_stakeall = 60 * 60 * 24; /* once per day */
	c.fallback_cpu_receiver = "updatethings"_n;
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
	s.current_cpu_contract = "cpu1.fusion"_n;
	s.next_stakeall_time = INITIAL_EPOCH_START_TIMESTAMP + 60 * 60 * 24; /* 1 day */
	states.set(s, _self);

	//create the first epoch

	epochs_t.emplace(get_self(), [&](auto &_e){
		_e.start_time = INITIAL_EPOCH_START_TIMESTAMP;
		/* unstake 3 days before epoch ends */
		_e.time_to_unstake = INITIAL_EPOCH_START_TIMESTAMP + (60 * 60 * 24 * 14) - (60 * 60 * 24 * 3);
		_e.cpu_wallet = "cpu1.fusion"_n;
		_e.wax_bucket = ZERO_WAX;
		_e.wax_to_refund = ZERO_WAX;
		/* redemption starts at the end of the epoch, ends 48h later */
		_e.redemption_period_start_time = INITIAL_EPOCH_START_TIMESTAMP + (60 * 60 * 24 * 14);
		_e.redemption_period_end_time = INITIAL_EPOCH_START_TIMESTAMP + (60 * 60 * 24 * 16);
		_e.total_cpu_funds_returned = ZERO_WAX;
		_e.total_added_to_redemption_bucket = ZERO_WAX;
	});	
}

ACTION fusion::inittop21(){
	require_auth(get_self());

	eosio::check(!top21_s.exists(), "top21 already exists");

	auto idx = _producers.get_index<"prototalvote"_n>();	

	using value_type = std::pair<eosio::producer_authority, uint16_t>;
	std::vector< value_type > top_producers;
	top_producers.reserve(21);	

	for( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < 21 && 0 < it->total_votes && it->active(); ++it ) {
		top_producers.emplace_back(
			eosio::producer_authority{
			   .producer_name = it->owner,
			   .authority     = it->get_producer_authority()
			},
			it->location
		);
	}

	if( top_producers.size() < MINIMUM_PRODUCERS_TO_VOTE_FOR ) {
		check( false, ("attempting to vote for " + std::to_string( top_producers.size() ) + " producers but need to vote for " + std::to_string( MINIMUM_PRODUCERS_TO_VOTE_FOR ) ).c_str() );
	}	

	std::sort(top_producers.begin(), top_producers.end(),
	    [](const value_type& a, const value_type& b) -> bool {
	        return a.first.producer_name.to_string() < b.first.producer_name.to_string();
	    }
	);	

	std::vector<eosio::name> producers_to_vote_for {};

	for(auto t : top_producers){
		producers_to_vote_for.push_back(t.first.producer_name);
	}

	top21 t{};
	t.block_producers = producers_to_vote_for;
	t.last_update = now();
	top21_s.set(t, _self);

}

/** 
 *  instaredeem
 *  by default, when users request redemptions, they get added to the queue
 *  depending on which epochs have the wax available for redemption
 *  there is 0 fee
 *  the instaredeem action allows users to redeem instantly using funds that 
 *  are inside the dapp contract (avaiable_for_rentals pool), assuming there are 
 *  enough funds to cover their redemption
 *  there is a 0.05% fee when using instaredeem. this allows the protocol to utilize
 *  funds that would normally be used for CPU rentals and staking, in a more efficient manner
 *  while also helping to maintain the LSWAX peg on the open market
 */
ACTION fusion::instaredeem(const eosio::name& user, const eosio::asset& swax_to_redeem){
	require_auth(user);
	sync_user(user);
	sync_epoch();

	//make sure the amount to redeem is not more than their balance
	auto staker = staker_t.require_find(user.value, "you are not staking any sWAX");
	check(staker->swax_balance >= swax_to_redeem, "you are trying to redeem more than you have");

    check( swax_to_redeem.amount > 0, "Must redeem a positive quantity" );
    check( swax_to_redeem.amount < MAX_ASSET_AMOUNT, "quantity too large" );

    eosio::asset new_sWAX_balance = staker->swax_balance - swax_to_redeem;

	//debit requested amount from their staked balance
	staker_t.modify(staker, same_payer, [&](auto &_s){
		_s.swax_balance -= swax_to_redeem;
		_s.last_update = now();
	});

	state s = states.get();

	check( s.wax_available_for_rentals.amount >= swax_to_redeem.amount, "not enough instaredeem funds available" );

	//debit the amount from s.wax_available_for_rentals
	s.wax_available_for_rentals.amount = safeSubInt64(s.wax_available_for_rentals.amount, swax_to_redeem.amount);

	//calculate the 0.05% fee
	//calculate the remainder for the user
	double procotol_fee_percentage = (double) 0.0005;
	double user_percentage = (double) 1 - procotol_fee_percentage;
	double protocol_fee_double = safeMulDouble( procotol_fee_percentage, (double) swax_to_redeem.amount );
	double amount_to_transfer_double = safeMulDouble( user_percentage, swax_to_redeem.amount );

	check( safeAddInt64( (int64_t) amount_to_transfer_double, (int64_t) protocol_fee_double ) <= swax_to_redeem.amount, "error calculating protocol fee" );

	//transfer the funds to the user
	transfer_tokens( user, asset( (int64_t) amount_to_transfer_double, WAX_SYMBOL), WAX_CONTRACT, std::string("your sWAX redemption from waxfusion.io - liquid staking protocol") );

	//add the 0.05% to the revenue_awaiting_distribution
	s.revenue_awaiting_distribution.amount = safeAddInt64( s.revenue_awaiting_distribution.amount, (int64_t) protocol_fee_double );

	//set the state
	states.set(s, _self);

    debit_user_redemptions_if_necessary(user, new_sWAX_balance);

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

	eosio::asset new_sWAX_balance = staker->swax_balance - quantity;

	//debit requested amount from their staked balance
	staker_t.modify(staker, same_payer, [&](auto &_s){
		_s.swax_balance -= quantity;
		_s.last_update = now();
	});

	//get the current state table
	state s = states.get();

	//calculate equivalent amount of lsWAX (BEFORE adjusting sWAX amounts)
	double lsWAX_per_sWAX;

	//need to account for initial period where the values are still 0
	if( s.liquified_swax.amount == 0 && s.swax_currently_backing_lswax.amount == 0 ){
		lsWAX_per_sWAX = (double) 1;
	} else {
		lsWAX_per_sWAX = safeDivDouble((double) s.liquified_swax.amount, (double) s.swax_currently_backing_lswax.amount);
	}

	double converted_lsWAX_amount = safeMulDouble( lsWAX_per_sWAX, (double) quantity.amount );
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

	debit_user_redemptions_if_necessary(user, new_sWAX_balance);

	return;
}

ACTION fusion::liquifyexact(const eosio::name& user, const eosio::asset& quantity, 
	const eosio::asset& expected_output, const double& max_slippage)
{

	require_auth(user);
    check(quantity.amount > 0, "Invalid quantity.");
    check(quantity.amount < MAX_ASSET_AMOUNT, "quantity too large");
    check(quantity.symbol == SWAX_SYMBOL, "only SWAX can be liquified");	

    check(expected_output.amount > 0, "Invalid output quantity.");
    check(expected_output.amount < MAX_ASSET_AMOUNT, "output quantity too large");
    check(expected_output.symbol == LSWAX_SYMBOL, "output symbol should be LSWAX");	    

    //process any payouts for this user since their last interaction
    sync_user(user);

    //make sure they have a row here
	auto staker = staker_t.require_find(user.value, "you have nothing to liquify");

	if(staker->swax_balance < quantity){
		check(false, "you are trying to liquify more than you have");
	}

	eosio::asset new_sWAX_balance = staker->swax_balance - quantity;

	//debit requested amount from their staked balance
	staker_t.modify(staker, same_payer, [&](auto &_s){
		_s.swax_balance -= quantity;
		_s.last_update = now();
	});

	//get the current state table
	state s = states.get();

	//calculate equivalent amount of lsWAX (BEFORE adjusting sWAX amounts)
	double lsWAX_per_sWAX;

	//need to account for initial period where the values are still 0
	if( s.liquified_swax.amount == 0 && s.swax_currently_backing_lswax.amount == 0 ){
		lsWAX_per_sWAX = (double) 1;
	} else {
		lsWAX_per_sWAX = safeDivDouble((double) s.liquified_swax.amount, (double) s.swax_currently_backing_lswax.amount);
	}

	double converted_lsWAX_amount = safeMulDouble( lsWAX_per_sWAX, (double) quantity.amount );
	int64_t converted_lsWAX_i64 = (int64_t) converted_lsWAX_amount;	

	check( max_slippage >= (double) 0 && max_slippage < (double) 1, "max slippage is out of range" );
	double minimum_output_percentage = (double) 1 - max_slippage;
	double minimum_output = safeMulDouble( (double) expected_output.amount, minimum_output_percentage );

	check( converted_lsWAX_i64 >= (int64_t) minimum_output, "output would be " + asset(converted_lsWAX_i64, LSWAX_SYMBOL).to_string() + " but expected " + asset(minimum_output, LSWAX_SYMBOL).to_string() );

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

	debit_user_redemptions_if_necessary(user, new_sWAX_balance);

	return;
}

/**
* reallocate
* used for taking any funds that were requested to be redeemed, but werent redeemed in time
*/ 

ACTION fusion::reallocate(){
	//should anyone be able to call this? all it does is move unredeemed to available_for_rental, so probably yes

	sync_epoch();

	//get the last epoch start time
	state s = states.get();
	config c = configs.get();

	//if now > epoch start time + 48h, it means redemption is over
	check( now() > s.last_epoch_start_time + c.redemption_period_length_seconds, "redemption period has not ended yet" );

	//move funds from redemption pool to rental pool
	check( s.wax_for_redemption.amount > 0, "there is no wax to reallocate" );

	s.wax_available_for_rentals.amount = safeAddInt64(s.wax_available_for_rentals.amount, s.wax_for_redemption.amount);
	s.wax_for_redemption = ZERO_WAX;

	states.set(s, _self);
}

ACTION fusion::redeem(const eosio::name& user){
	require_auth(user);
	sync_user(user);
	sync_epoch();

	//find out if there is a current redemption period, and when
	state s = states.get();
	config c = configs.get();

	uint64_t redemption_start_time = s.last_epoch_start_time;
	uint64_t redemption_end_time = s.last_epoch_start_time + c.redemption_period_length_seconds;

	uint64_t epoch_to_claim_from = s.last_epoch_start_time - c.seconds_between_epochs;
 
	check( now() < redemption_end_time, 
		( "next redemption does not start until " + std::to_string(s.last_epoch_start_time + c.seconds_between_epochs) ).c_str() 
	);

	//find if the user has a request for this period
	requests_tbl requests_t = requests_tbl(get_self(), user.value);

	auto req_itr = requests_t.require_find(epoch_to_claim_from, "you don't have a redemption request for the current redemption period");

	//if they do, make sure the amount is <= their swax amount
	auto staker = staker_t.require_find(user.value, "you are not staking any sWAX");

	check( req_itr->wax_amount_requested.amount <= staker->swax_balance.amount, "you are trying to redeem more than you have" );

	//make sure s.wax_for_redemption has enough for them (it always should!)
	check( s.wax_for_redemption >= req_itr->wax_amount_requested, "not enough wax in the redemption pool" );

	//subtract the amount from s.wax_for_redemption
	s.wax_for_redemption.amount = safeSubInt64(s.wax_for_redemption.amount, req_itr->wax_amount_requested.amount);

	//subtract the requested amount from their swax balance
	asset updated_swax_balance = staker->swax_balance;
	updated_swax_balance.amount = safeSubInt64(updated_swax_balance.amount, req_itr->wax_amount_requested.amount);

	staker_t.modify(staker, same_payer, [&](auto &_s){
		_s.swax_balance = updated_swax_balance;
	});

	//retire the sWAX
	retire_swax(req_itr->wax_amount_requested.amount);

	//update the swax_currently_earning amount
	s.swax_currently_earning.amount = safeSubInt64(s.swax_currently_earning.amount, req_itr->wax_amount_requested.amount);

	//transfer wax to the user
	transfer_tokens( user, req_itr->wax_amount_requested, WAX_CONTRACT, std::string("your sWAX redemption from waxfusion.io - liquid staking protocol") );

	//erase the request
	req_itr = requests_t.erase(req_itr);
}


ACTION fusion::removeadmin(const eosio::name& admin_to_remove){
	require_auth(_self);

	config c = configs.get();

    auto itr = std::remove(c.admin_wallets.begin(), c.admin_wallets.end(), admin_to_remove);

    if (itr != c.admin_wallets.end()) {
        c.admin_wallets.erase(itr, c.admin_wallets.end());
        configs.set(c, _self);
    } else {
        check(false, (admin_to_remove.to_string() + " is not an admin").c_str());
    }
}

/**
* reqredeem (request redeem)
* initiates a redemption request
* the contract will automatically figure out which epoch(s) have enough wax available
* the user must claim (redeem) their wax within 2 days of it becoming available, or it will be restaked
* 
*/ 

ACTION fusion::reqredeem(const eosio::name& user, const eosio::asset& swax_to_redeem){
	require_auth(user);
	sync_user(user);
	sync_epoch();

	//make sure the amount to redeem is not more than their balance
	auto staker = staker_t.require_find(user.value, "you are not staking any sWAX");
	check(staker->swax_balance >= swax_to_redeem, "you are trying to redeem more than you have");

    check( swax_to_redeem.amount > 0, "Must redeem a positive quantity" );
    check( swax_to_redeem.amount < MAX_ASSET_AMOUNT, "quantity too large" );

	/** 
	* figure out which epoch(s) to take this from, update the epoch(s) to reflect the request
	* first need to find out the epoch linked to the unstake action that is closest to taking place
	* does that have any wax available?
	* if so, put as much of this request into that epoch as possible
	* if there is anything left, then check the next epoch too
	* if that has anything available, repeat
	* can do this one more time if there is a 3rd epoch available
	*/

	state s = states.get();
	config c = configs.get();

	requests_tbl requests_t = requests_tbl(get_self(), user.value);

	bool request_can_be_filled = false;
	eosio::asset remaining_amount_to_fill = swax_to_redeem;
	uint64_t epoch_to_request_from = s.last_epoch_start_time - c.seconds_between_epochs;

	std::vector<uint64_t> epochs_to_check = {
		epoch_to_request_from,
		epoch_to_request_from + c.seconds_between_epochs,
		epoch_to_request_from + ( c.seconds_between_epochs * 2 )
	};

	/** 
	* loop through the 3 redemption periods and if the user has any reqs,
	* delete them and sub the amounts from epoch_itr->wax_to_refund
	*/

	for(uint64_t ep : epochs_to_check){
		auto epoch_itr = epochs_t.find(ep);

		if(epoch_itr != epochs_t.end()){

			auto req_itr = requests_t.find(ep);	

			if(req_itr != requests_t.end()){
				//there is a pending request

				//subtract the pending amount from epoch_itr->wax_to_refund
				int64_t updated_refunding_amount = safeSubInt64(epoch_itr->wax_to_refund.amount, req_itr->wax_amount_requested.amount);

				epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
					_e.wax_to_refund = asset(updated_refunding_amount, WAX_SYMBOL);
				});

				//erase the request
				req_itr = requests_t.erase(req_itr);
			}
		}	
	}

	/**
	* now loop through them again and process them
	* if request becomes filled, break out of the loop
	*/

	for(uint64_t ep : epochs_to_check){
		auto epoch_itr = epochs_t.find(ep);

		if(epoch_itr != epochs_t.end()){

			//see if the deadline for redeeming has passed yet
			if(epoch_itr->time_to_unstake > now()){

				if(epoch_itr->wax_to_refund < epoch_itr->wax_bucket){
					//there are still funds available for redemption

					int64_t amount_available = safeSubInt64(epoch_itr->wax_bucket.amount, epoch_itr->wax_to_refund.amount);

					if(amount_available >= remaining_amount_to_fill.amount){
						//this epoch has enough to cover the whole request
						request_can_be_filled = true;

						int64_t updated_refunding_amount = safeAddInt64(epoch_itr->wax_to_refund.amount, remaining_amount_to_fill.amount);

						//add the amount to the epoch's wax_to_refund
						epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
							_e.wax_to_refund = asset(updated_refunding_amount, WAX_SYMBOL);
						});

						/** 
						* INSERT this request into the request_tbl
						* (any previous records should have been deleted first)
						*/

						auto req_itr = requests_t.find(ep);

						check( req_itr == requests_t.end(), "user has an existing redemption request in this epoch" );

						requests_t.emplace(user, [&](auto &_r){
							_r.epoch_id = ep;
							_r.wax_amount_requested = asset(remaining_amount_to_fill.amount, WAX_SYMBOL);
						});


					} else {
						//this epoch has some funds, but not enough for the whole request
						int64_t updated_refunding_amount = safeAddInt64(epoch_itr->wax_to_refund.amount, amount_available);

						//debit the amount remaining so we are checking an updated number on the next loop
						remaining_amount_to_fill.amount = safeSubInt64(remaining_amount_to_fill.amount, amount_available);

						epochs_t.modify(epoch_itr, get_self(), [&](auto &_e){
							_e.wax_to_refund = asset(updated_refunding_amount, WAX_SYMBOL);
						});

						auto req_itr = requests_t.find(ep);

						check( req_itr == requests_t.end(), "user has an existing redemption request in this epoch" );
						
						requests_t.emplace(user, [&](auto &_r){
							_r.epoch_id = ep;
							_r.wax_amount_requested = asset(amount_available, WAX_SYMBOL);
						});
					}
				}
			}

		}	

		if( request_can_be_filled ) break;
	}	 

	if( !request_can_be_filled ){
		/** make sure there is enough wax in available_for_rentals pool to cover the remainder
		 *  there should be 0 cases where this this check fails
		 *  if epochs cant cover it, there should always be enough in available for rentals to 
		 *  cover the remainder
		 */ 

		check( s.wax_available_for_rentals.amount >= remaining_amount_to_fill.amount, "Request amount is greater than amount in epochs and rental pool" );

		s.wax_available_for_rentals.amount = safeSubInt64( s.wax_available_for_rentals.amount, remaining_amount_to_fill.amount );
		states.set(s, _self);

		//debit the swax amount from the user's balance
		staker_t.modify(staker, same_payer, [&](auto &_s){
			_s.swax_balance.amount = safeSubInt64( _s.swax_balance.amount, remaining_amount_to_fill.amount );
		});

		transfer_tokens( user, asset( remaining_amount_to_fill.amount, WAX_SYMBOL ), WAX_CONTRACT, std::string("your redemption from waxfusion.io - liquid staking protocol") );
	}

}

ACTION fusion::rmvcpucntrct(const eosio::name& contract_to_remove){
	require_auth(_self);

	config c = configs.get();

    auto itr = std::remove(c.cpu_contracts.begin(), c.cpu_contracts.end(), contract_to_remove);

    if (itr != c.cpu_contracts.end()) {
        c.cpu_contracts.erase(itr, c.cpu_contracts.end());
        configs.set(c, _self);
    } else {
        check(false, (contract_to_remove.to_string() + " is not a cpu contract").c_str());
    }
}

ACTION fusion::setfallback(const eosio::name& caller, const eosio::name& receiver){
	require_auth(caller);
	check( is_an_admin(caller), "this action requires auth from one of the admin_wallets in the config table" );
	check( is_account(receiver), "cpu receiver is not a wax account" );

	config c = configs.get();
	c.fallback_cpu_receiver = receiver;
	configs.set(c, _self);
}

ACTION fusion::setrentprice(const eosio::name& caller, const eosio::asset& cost_to_rent_1_wax){
	require_auth(caller);
	check( is_an_admin(caller), "this action requires auth from one of the admin_wallets in the config table" );
	check( cost_to_rent_1_wax.amount > 0, "cost must be positive" );
	check( cost_to_rent_1_wax.symbol == WAX_SYMBOL, "symbol and precision must match WAX" );

	state s = states.get();
	s.cost_to_rent_1_wax = cost_to_rent_1_wax;
	states.set(s, _self);

	action(permission_level{get_self(), "active"_n}, POL_CONTRACT,"setrentprice"_n,std::tuple{ cost_to_rent_1_wax }).send();
}

/**
* stake
* this just opens a row if necessary so we can react to transfers etc
* also syncs the user if they exist
*/
  
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

/**
* stakeallcpu
* once every 24h, this can be called to take any un-rented wax and just stake it so it earns the normal amount
*/ 

ACTION fusion::stakeallcpu(){
	sync_epoch();

	//get the last epoch start time
	state s = states.get();
	config c = configs.get();

	//if now > epoch start time + 48h, it means redemption is over
	check( now() >= s.next_stakeall_time, ( "next stakeall time is not until " + std::to_string(s.next_stakeall_time) ).c_str() );

	if(s.wax_available_for_rentals.amount > 0){

		//then we can just get the next contract in line and next epoch in line
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

		uint64_t next_epoch_start_time = s.last_epoch_start_time + c.seconds_between_epochs;

		transfer_tokens( next_cpu_contract, s.wax_available_for_rentals, WAX_CONTRACT, cpu_stake_memo(c.fallback_cpu_receiver, next_epoch_start_time) );

		//upsert the epoch that it was staked to, so it reflects the added wax
		auto next_epoch_itr = epochs_t.find(next_epoch_start_time);

		if(next_epoch_itr == epochs_t.end()){
			//create new epoch
			epochs_t.emplace(get_self(), [&](auto &_e){
				_e.start_time = next_epoch_start_time;
				/* unstake 3 days before epoch ends */
				_e.time_to_unstake = next_epoch_start_time + c.cpu_rental_epoch_length_seconds - (60 * 60 * 24 * 3);
				_e.cpu_wallet = next_cpu_contract;
				_e.wax_bucket = s.wax_available_for_rentals;
				_e.wax_to_refund = ZERO_WAX;
				/* redemption starts at the end of the epoch, ends 48h later */
				_e.redemption_period_start_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds;
				_e.redemption_period_end_time = next_epoch_start_time + c.cpu_rental_epoch_length_seconds + c.redemption_period_length_seconds;
				_e.total_cpu_funds_returned = ZERO_WAX;
				_e.total_added_to_redemption_bucket = ZERO_WAX;
			});

		} else {
			//update epoch
			asset current_wax_bucket = next_epoch_itr->wax_bucket;
			current_wax_bucket.amount = safeAddInt64(current_wax_bucket.amount, s.wax_available_for_rentals.amount);
			epochs_t.modify(next_epoch_itr, get_self(), [&](auto &_e){
				_e.wax_bucket = current_wax_bucket;
			});
		}

		//reset it to 0
		s.wax_available_for_rentals = ZERO_WAX;
	}

	//update the next_stakeall_time
	s.next_stakeall_time += c.seconds_between_stakeall;
	states.set(s, _self);
}

/**
* sync
* this only exists to keep data refreshed and make it easier for front ends to display fresh data
* it's not necessary for the dapp to function properly
* therefore it requires admin auth to avoid random people spamming the network and running this constantly
*/ 

ACTION fusion::sync(const eosio::name& caller){
	require_auth( caller );
	check( is_an_admin( caller ), ( caller.to_string() + " is not an admin" ).c_str() );
	sync_epoch();
}

ACTION fusion::unstakecpu(){
	//anyone can call this

	sync_epoch();

	//get the most recently started epoch
	state s = states.get();
	config c = configs.get();

	//the only epoch that should ever need unstaking is the one that started prior to current epoch
	//calculate the epoch prior to the most recently started one
	uint64_t epoch_to_check = s.last_epoch_start_time - c.seconds_between_epochs;

	//if the unstake time is <= now, look up its cpu contract is delband table
	auto epoch_itr = epochs_t.require_find( epoch_to_check, ("could not find epoch " + std::to_string( epoch_to_check ) ).c_str() );

	check( epoch_itr->time_to_unstake <= now(), ("can not unstake until another " + std::to_string( epoch_itr-> time_to_unstake - now() ) + " seconds has passed").c_str() );

	del_bandwidth_table del_tbl( SYSTEM_CONTRACT, epoch_itr->cpu_wallet.value );

	if( del_tbl.begin() == del_tbl.end() ){
		check( false, ( epoch_itr->cpu_wallet.to_string() + " has nothing to unstake" ).c_str() );
	}	

	action(permission_level{get_self(), "active"_n}, epoch_itr->cpu_wallet,"unstakebatch"_n,std::tuple{ (int) 1000 }).send();
}

ACTION fusion::updatetop21(){
	top21 t = top21_s.get();

	check( t.last_update + (60 * 60 * 24) <= now(), "hasn't been 24h since last top21 update" );

	auto idx = _producers.get_index<"prototalvote"_n>();	

	using value_type = std::pair<eosio::producer_authority, uint16_t>;
	std::vector< value_type > top_producers;
	top_producers.reserve(21);	

	for( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < 21 && 0 < it->total_votes && it->active(); ++it ) {
		top_producers.emplace_back(
			eosio::producer_authority{
			   .producer_name = it->owner,
			   .authority     = it->get_producer_authority()
			},
			it->location
		);
	}

	if( top_producers.size() < MINIMUM_PRODUCERS_TO_VOTE_FOR ) {
		check( false, ("attempting to vote for " + std::to_string( top_producers.size() ) + " producers but need to vote for " + std::to_string( MINIMUM_PRODUCERS_TO_VOTE_FOR ) ).c_str() );
	}	

	std::sort(top_producers.begin(), top_producers.end(),
	    [](const value_type& a, const value_type& b) -> bool {
	        return a.first.producer_name.to_string() < b.first.producer_name.to_string();
	    }
	);	

	std::vector<eosio::name> producers_to_vote_for {};

	for(auto t : top_producers){
		producers_to_vote_for.push_back(t.first.producer_name);
	}

	t.block_producers = producers_to_vote_for;
	t.last_update = now();
	top21_s.set(t, _self);
}