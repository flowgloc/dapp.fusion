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
		states(receiver, receiver.value)
		{}		

		//Main Actions
		ACTION claimrewards(const eosio::name& user);
		ACTION distribute();
		ACTION initconfig();
		ACTION liquify(const eosio::name& user, const eosio::asset& quantity);
		ACTION reallocate();
		ACTION redeem(const eosio::name& user);
		ACTION reqredeem(const eosio::name& user, const eosio::asset& swax_to_redeem);
		ACTION stake(const eosio::name& user);


		//Notifications
		[[eosio::on_notify("*::transfer")]] void receive_token_transfer(name from, name to, eosio::asset quantity, std::string memo);


	private:

		//Singletons
		config_singleton configs;
		state_singleton states;

		//Multi Index Tables
		cpu_table cpu_t = cpu_table(get_self(), get_self().value);
		eco_table eco_t = eco_table(get_self(), get_self().value);
		epochs_table epochs_t = epochs_table(get_self(), get_self().value);
		snaps_table snaps_t = snaps_table(get_self(), get_self().value);
		staker_table staker_t = staker_table(get_self(), get_self().value);


		//Functions
		void issue_lswax(const int64_t& amount, const eosio::name& receiver);
		void issue_swax(const int64_t& amount);
		bool memo_is_expected(const std::string& memo);
		uint64_t now();
		void retire_lswax(const int64_t& amount);
		void sync_epoch();
		void sync_user(const eosio::name& user);
		void transfer_tokens(const name& user, const asset& amount_to_send, const name& contract, const std::string& memo);
		void validate_token(const eosio::symbol& symbol, const eosio::name& contract);
		void zero_distribution();

		//Safemath
		int64_t safeAddInt64(const int64_t& a, const int64_t& b);
		double safeDivDouble(const double& a, const double& b);
		int64_t safeSubInt64(const int64_t& a, const int64_t& b);
};
