#pragma once

#define CONTRACT_NAME "fusion"
#define mix64to128(a, b) (uint128_t(a) << 64 | uint128_t(b))

#include <eosio/eosio.hpp>
#include <eosio/print.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/symbol.hpp>
#include <string>
#include <eosio/crypto.hpp>
#include <eosio/transaction.hpp>
#include <eosio/singleton.hpp>
#include <eosio/binary_extension.hpp>
#include <eosio/producer_schedule.hpp>
#include<map>
#include "structs.hpp"
#include "constants.hpp"
#include "tables.hpp"

using namespace eosio;

CONTRACT fusion : public contract {
	public:
		using contract::contract;

		fusion(name receiver, name code, datastream<const char *> ds):
		contract(receiver, code, ds),
		configs(receiver, receiver.value),
		states(receiver, receiver.value),
		top21_s(receiver, receiver.value)
		{}		

		//Main Actions
		ACTION addadmin(const eosio::name& admin_to_add);
		ACTION addcpucntrct(const eosio::name& contract_to_add);
		ACTION claimgbmvote(const eosio::name& cpu_contract);
		ACTION claimrefunds();
		ACTION claimrewards(const eosio::name& user);
		ACTION claimswax(const eosio::name& user);
		ACTION distribute();
		ACTION initconfig();
		ACTION inittop21();
		ACTION liquify(const eosio::name& user, const eosio::asset& quantity);
		ACTION liquifyexact(const eosio::name& user, const eosio::asset& quantity, 
			const eosio::asset& expected_output, const double& max_slippage);
		ACTION reallocate();
		ACTION redeem(const eosio::name& user);
		ACTION removeadmin(const eosio::name& admin_to_remove);
		ACTION reqredeem(const eosio::name& user, const eosio::asset& swax_to_redeem);
		ACTION rmvcpucntrct(const eosio::name& contract_to_remove);
		ACTION setfallback(const eosio::name& caller, const eosio::name& receiver);
		ACTION setrentprice(const eosio::name& caller, const eosio::asset& cost_to_rent_1_wax);
		ACTION stake(const eosio::name& user);
		ACTION stakeallcpu();
		ACTION sync(const eosio::name& caller);
		ACTION unstakecpu();
		ACTION updatetop21();

		//Notifications
		[[eosio::on_notify("*::transfer")]] void receive_token_transfer(name from, name to, eosio::asset quantity, std::string memo);


	private:

		//Singletons
		config_singleton configs;
		state_singleton states;
		top21_singleton top21_s;

		//Multi Index Tables
		debug_table debug_t = debug_table(get_self(), get_self().value);
		eco_table eco_t = eco_table(get_self(), get_self().value);
		epochs_table epochs_t = epochs_table(get_self(), get_self().value);
		snaps_table snaps_t = snaps_table(get_self(), get_self().value);
		producers_table _producers = producers_table(SYSTEM_CONTRACT, SYSTEM_CONTRACT.value);
		staker_table staker_t = staker_table(get_self(), get_self().value);


		//Functions
		std::string cpu_stake_memo(const eosio::name& cpu_receiver, const uint64_t& epoch_timestamp);
		std::vector<std::string> get_words(std::string memo);
		bool is_an_admin(const eosio::name& user);
		bool is_cpu_contract(const eosio::name& contract);
		void issue_lswax(const int64_t& amount, const eosio::name& receiver);
		void issue_swax(const int64_t& amount);
		bool memo_is_expected(const std::string& memo);
		uint64_t now();
		void retire_lswax(const int64_t& amount);
		void retire_swax(const int64_t& amount);
		void sync_epoch();
		void sync_user(const eosio::name& user);
		void transfer_tokens(const name& user, const asset& amount_to_send, const name& contract, const std::string& memo);
		void validate_token(const eosio::symbol& symbol, const eosio::name& contract);
		void zero_distribution();

		//Safemath
		int64_t safeAddInt64(const int64_t& a, const int64_t& b);
		double safeDivDouble(const double& a, const double& b);
		uint64_t safeMulUInt64(const uint64_t& a, const uint64_t& b);
		int64_t safeSubInt64(const int64_t& a, const int64_t& b);
};
